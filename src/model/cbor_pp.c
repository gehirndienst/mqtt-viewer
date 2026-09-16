// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "model/cbor_pp.h"
#include "model/util.h"

#define CBOR_MAX_TAGS 8
#define CBOR_TAG_PREFIX_LEN 192 // CBOR_MAX_TAGS x "18446744073709551615(" + NUL

typedef struct {
    const uint8_t* src;
    size_t len;
    size_t pos;
    JsonPP* pp;
} CborDec;

typedef struct {
    uint8_t major;
    uint8_t info; // additional information, 0..31
    uint64_t arg; // value / length / count / tag number; unset when indefinite
    bool indefinite;
} CborHead;

// bounded text builder: drops what does not fit, always NUL-terminated
typedef struct {
    char* buf;
    size_t cap;
    size_t len;
} CborOut;

static bool decode_item(CborDec* d, const char* key, const char* tag_prefix, int tags);

static void cbor_out_ch(CborOut* o, char c) {
    if (o->len + 1 < o->cap) o->buf[o->len++] = c;
    o->buf[o->len] = '\0';
}

static void cbor_out_str(CborOut* o, const char* s) {
    while (*s) cbor_out_ch(o, *s++);
}

// A closing quote/paren must survive truncation so a cut string still reads as one token
static void cbor_out_close(CborOut* o, char c) {
    if (o->len + 1 < o->cap) {
        o->buf[o->len++] = c;
    } else if (o->cap >= 2) {
        o->buf[o->cap - 2] = c;
    }
    o->buf[o->len] = '\0';
}

static bool read_head(CborDec* d, CborHead* h) {
    if (d->pos >= d->len) return false;

    uint8_t ib = d->src[d->pos++];
    h->major = ib >> 5;
    h->info = ib & 0x1f;
    h->arg = 0;
    h->indefinite = false;
    if (h->info < 24) {
        h->arg = h->info;
        return true;
    }

    size_t n;
    switch (h->info) {
        case 24:
            n = 1;
            break;
        case 25:
            n = 2;
            break;
        case 26:
            n = 4;
            break;
        case 27:
            n = 8;
            break;
        case 31:
            h->indefinite = true;
            return true;
        default:
            return false; // 28..30 are reserved
    }

    if (d->len - d->pos < n) return false;

    for (size_t i = 0; i < n; i++) h->arg = (h->arg << 8) | d->src[d->pos++];

    return true;
}

static double decode_half(uint16_t half) {
    int exp = (half >> 10) & 0x1f;
    int mant = half & 0x3ff;
    double val;
    if (exp == 0) {
        val = ldexp(mant, -24);
    } else if (exp != 31) {
        val = ldexp(mant + 1024, exp - 25);
    } else {
        val = mant == 0 ? INFINITY : NAN;
    }
    return (half & 0x8000) ? -val : val;
}

static double decode_single(uint32_t bits) {
    float f;
    memcpy(&f, &bits, sizeof(f));
    return (double)f;
}

static double decode_double(uint64_t bits) {
    double v;
    memcpy(&v, &bits, sizeof(v));
    return v;
}

// Shortest %g that round-trips; a trailing ".0" keeps floats visibly distinct from integers
static void fmt_double(double v, char* out, size_t cap) {
    if (isnan(v)) {
        util_str_copy(out, cap, "NaN");
        return;
    }

    if (isinf(v)) {
        util_str_copy(out, cap, v < 0 ? "-Infinity" : "Infinity");
        return;
    }

    snprintf(out, cap, "%.15g", v);
    if (strtod(out, NULL) != v) snprintf(out, cap, "%.17g", v);
    if (!strpbrk(out, ".eE")) {
        size_t n = strlen(out);
        if (n + 2 < cap) memcpy(out + n, ".0", 3);
    }
}

// one definite chunk: bytes as lowercase hex, text with JSON-style escapes
static bool append_chunk(CborDec* d, uint64_t n, bool bytes, CborOut* o) {
    if (n > d->len - d->pos) return false;
    static const char hex[] = "0123456789abcdef";
    for (uint64_t i = 0; i < n; i++) {
        uint8_t c = d->src[d->pos++];
        if (bytes) {
            cbor_out_ch(o, hex[c >> 4]);
            cbor_out_ch(o, hex[c & 15]);
        } else if (c == '"' || c == '\\') {
            cbor_out_ch(o, '\\');
            cbor_out_ch(o, (char)c);
        } else if (c == '\n') {
            cbor_out_str(o, "\\n");
        } else if (c == '\r') {
            cbor_out_str(o, "\\r");
        } else if (c == '\t') {
            cbor_out_str(o, "\\t");
        } else if (c < 0x20) {
            char esc[8];
            snprintf(esc, sizeof(esc), "\\u%04x", c);
            cbor_out_str(o, esc);
        } else {
            cbor_out_ch(o, (char)c);
        }
    }
    return true;
}

// Definite string, or a run of definite same-type chunks ended by a break
static bool append_string(CborDec* d, const CborHead* h, CborOut* o) {
    bool bytes = h->major == 2;
    if (!h->indefinite) return append_chunk(d, h->arg, bytes, o);
    for (;;) {
        CborHead c;
        if (!read_head(d, &c)) return false;
        if (c.major == 7 && c.info == 31) return true;
        if (c.major != h->major || c.indefinite) return false;
        if (!append_chunk(d, c.arg, bytes, o)) return false;
    }
}

static bool format_simple(const CborHead* h, char* val, size_t cap, bool* numeric) {
    switch (h->info) {
        case 20:
            util_str_copy(val, cap, "false");
            return true;
        case 21:
            util_str_copy(val, cap, "true");
            return true;
        case 22:
            util_str_copy(val, cap, "null");
            return true;
        case 23:
            util_str_copy(val, cap, "undefined");
            return true;
        case 24:
            if (h->arg < 32) return false;
            snprintf(val, cap, "simple(%" PRIu64 ")", h->arg);
            return true;
        case 25:
        case 26:
        case 27: {
            double v = h->info == 25 ? decode_half((uint16_t)h->arg)
                : h->info == 26      ? decode_single((uint32_t)h->arg)
                                     : decode_double(h->arg);
            fmt_double(v, val, cap);
            *numeric = isfinite(v);
            return true;
        }
        case 31:
            return false; // break outside an indefinite container
        default:
            snprintf(val, cap, "simple(%u)", (unsigned)h->info);
            return true;
    }
}

// Majors 0-3 and 7. Containers and tags are handled by decode_item()
static bool format_scalar(CborDec* d, const CborHead* h, char* val, size_t cap, JsonPPValKind* kind, bool* numeric) {
    CborOut o = {val, cap, 0};
    val[0] = '\0';
    *kind = JSON_PP_VAL_ATOM;
    *numeric = false;

    switch (h->major) {
        case 0:
            if (h->indefinite) return false;
            snprintf(val, cap, "%" PRIu64, h->arg);
            *numeric = true;
            return true;
        case 1:
            if (h->indefinite) return false;
            if (h->arg == UINT64_MAX) {
                util_str_copy(val, cap, "-18446744073709551616");
            } else {
                snprintf(val, cap, "-%" PRIu64, h->arg + 1);
            }
            *numeric = true;
            return true;
        case 2:
            cbor_out_str(&o, "h'");
            if (!append_string(d, h, &o)) return false;
            cbor_out_close(&o, '\'');
            return true;
        case 3:
            cbor_out_ch(&o, '"');
            if (!append_string(d, h, &o)) return false;
            cbor_out_close(&o, '"');
            *kind = JSON_PP_VAL_STRING;
            *numeric = json_pp_quoted_is_numeric(val);
            return true;
        case 7:
            return format_simple(h, val, cap, numeric);
        default:
            return false;
    }
}

// Map keys: scalars only. Text keys are used verbatim (no quotes) so dot paths read "a.b" like JSON's
static bool decode_key(CborDec* d, char* seg, size_t cap) {
    CborHead h;
    if (!read_head(d, &h)) return false;
    if (h.major == 3) {
        CborOut o = {seg, cap, 0};
        seg[0] = '\0';
        return append_string(d, &h, &o);
    }

    if (h.major >= 4 && h.major <= 6) return false;

    JsonPPValKind kind;
    bool numeric;
    return format_scalar(d, &h, seg, cap, &kind, &numeric);
}

static bool decode_container(CborDec* d, const CborHead* h, const char* key, const char* tag_prefix, int tags) {
    bool is_map = h->major == 5;
    if (d->pp->depth + 1 >= JSON_PP_MAX_DEPTH) return false;

    char open[CBOR_TAG_PREFIX_LEN + 4];
    snprintf(open, sizeof(open), "%s%s%s", tag_prefix, is_map ? "{" : "[", h->indefinite ? "_" : "");
    json_pp_emit(d->pp, key, open, JSON_PP_VAL_PUNCT, false);
    d->pp->depth++;

    // every entry costs at least one byte (two for a map pair) - reject absurd counts before looping
    size_t remaining = d->len - d->pos;
    if (!h->indefinite && h->arg > (is_map ? remaining / 2 : remaining)) return false;

    for (uint64_t i = 0; h->indefinite || i < h->arg; i++) {
        if (h->indefinite) {
            if (d->pos >= d->len) return false;
            if (d->src[d->pos] == 0xff) {
                d->pos++;
                break;
            }
        }
        if (i > 0) json_pp_mark_trailing_comma(d->pp);

        char seg[JSON_PP_KEY_LEN];
        if (is_map) {
            if (!decode_key(d, seg, sizeof(seg))) return false;
        } else {
            snprintf(seg, sizeof(seg), "%" PRIu64, i);
        }
        size_t mark = json_pp_path_push(d->pp, seg);
        if (!decode_item(d, is_map ? seg : "", "", 0)) return false;
        json_pp_path_pop(d->pp, mark);
    }

    d->pp->depth--;
    char close[CBOR_MAX_TAGS + 2];
    CborOut o = {close, sizeof(close), 0};
    close[0] = '\0';
    cbor_out_ch(&o, is_map ? '}' : ']');
    for (int t = 0; t < tags; t++) cbor_out_ch(&o, ')');
    json_pp_emit(d->pp, "", close, JSON_PP_VAL_PUNCT, false);
    return true;
}

static bool decode_item(CborDec* d, const char* key, const char* tag_prefix, int tags) {
    CborHead h;
    if (!read_head(d, &h)) return false;

    if (h.major == 6) {
        if (h.indefinite || tags >= CBOR_MAX_TAGS) return false;
        char prefix[CBOR_TAG_PREFIX_LEN];
        snprintf(prefix, sizeof(prefix), "%s%" PRIu64 "(", tag_prefix, h.arg);
        return decode_item(d, key, prefix, tags + 1);
    }
    if (h.major == 4 || h.major == 5) return decode_container(d, &h, key, tag_prefix, tags);

    char text[JSON_PP_VAL_LEN];
    JsonPPValKind kind;
    bool numeric;
    if (!format_scalar(d, &h, text, sizeof(text), &kind, &numeric)) return false;

    char val[JSON_PP_VAL_LEN];
    CborOut o = {val, sizeof(val), 0};
    val[0] = '\0';
    cbor_out_str(&o, tag_prefix);
    cbor_out_str(&o, text);
    for (int t = 0; t < tags; t++) cbor_out_close(&o, ')');
    json_pp_emit(d->pp, key, val, kind, numeric && tags == 0);
    return true;
}

bool cbor_pp_run(JsonPP* pp, const uint8_t* src, size_t len) {
    json_pp_begin(pp);
    CborDec d = {.src = src, .len = len, .pos = 0, .pp = pp};
    bool ok = len > 0 && decode_item(&d, "", "", 0) && d.pos == len;
    if (!ok) json_pp_begin(pp); // strict: leave no partial tree, depth or path behind
    return ok;
}

bool cbor_pp_run_cached(JsonPP* pp, const uint8_t* src, size_t len, uint64_t key) {
    const char* csrc = (const char*)src;
    if (pp->cache_src != csrc || pp->cache_key != key) {
        cbor_pp_run(pp, src, len);
        json_pp_mark_cached(pp, csrc, key);
    }
    return pp->line_count > 0;
}
