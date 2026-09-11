// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "model/json_pp.h"
#include "model/util.h"

static void format_value(JsonPP* pp, const char* key);

static void skip_ws(JsonPP* pp) {
    while (pp->pos < pp->src_len) {
        char c = pp->src[pp->pos];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
        pp->pos++;
    }
}

static void scan_string(JsonPP* pp, char* buf, int buf_size) {
    int start = pp->pos;
    pp->pos++;
    while (pp->pos < pp->src_len) {
        if (pp->src[pp->pos] == '\\' && pp->pos + 1 < pp->src_len) {
            pp->pos += 2;
            continue;
        }

        if (pp->src[pp->pos] == '"') {
            pp->pos++;
            break;
        }

        pp->pos++;
    }

    int copy_len = pp->pos - start;
    if (copy_len > buf_size - 1) copy_len = buf_size - 1;
    memcpy(buf, pp->src + start, (size_t)copy_len);
    buf[copy_len] = '\0';
}

static void scan_atom(JsonPP* pp, char* buf, int buf_size) {
    int start = pp->pos;

    while (pp->pos < pp->src_len) {
        char c = pp->src[pp->pos];
        if (c == ',' || c == '}' || c == ']' || c == '{' || c == '[' || c == ' ' || c == '\t' || c == '\n' || c == '\r')
            break;
        pp->pos++;
    }

    int copy_len = pp->pos - start;
    if (copy_len > buf_size - 1) copy_len = buf_size - 1;
    memcpy(buf, pp->src + start, (size_t)copy_len);
    buf[copy_len] = '\0';
}

static size_t path_push(JsonPP* pp, const char* seg) {
    size_t mark = pp->path_len;
    if (!seg || !seg[0]) return mark;

    size_t need_dot = (pp->path_len > 0) ? 1 : 0;
    size_t seg_len = strlen(seg);

    if (pp->path_len + need_dot + seg_len + 1 > sizeof(pp->path)) return mark;
    if (need_dot) pp->path[pp->path_len++] = '.';

    memcpy(pp->path + pp->path_len, seg, seg_len);
    pp->path_len += seg_len;
    pp->path[pp->path_len] = '\0';
    return mark;
}

static void path_pop(JsonPP* pp, size_t mark) {
    pp->path_len = mark;
    pp->path[pp->path_len] = '\0';
}

static void emit_line(JsonPP* pp, const char* key, const char* val, JsonPPValKind kind, bool is_numeric) {
    if (pp->line_count >= JSON_PP_MAX_LINES) return;
    JsonPPLine* line = &pp->lines[pp->line_count++];
    line->depth = pp->depth;
    util_str_copy(line->key, sizeof(line->key), key);
    util_str_copy(line->sep, sizeof(line->sep), key[0] ? ": " : "");
    util_str_copy(line->val, sizeof(line->val), val);
    line->trail[0] = '\0';
    line->val_kind = kind;
    line->is_numeric = is_numeric;
    util_str_copy(line->dot_path, sizeof(line->dot_path), pp->path);
}

static void mark_trailing_comma(JsonPP* pp) {
    if (pp->line_count > 0) util_str_copy(pp->lines[pp->line_count - 1].trail, sizeof(pp->lines[0].trail), ",");
}

static void format_object_contents(JsonPP* pp) {
    skip_ws(pp);
    while (pp->pos < pp->src_len && pp->src[pp->pos] != '}' && pp->line_count < JSON_PP_MAX_LINES) {
        skip_ws(pp);
        if (pp->pos >= pp->src_len || pp->src[pp->pos] == '}') break;

        char key[JSON_PP_KEY_LEN] = "";
        if (pp->src[pp->pos] == '"') scan_string(pp, key, sizeof(key));
        skip_ws(pp);
        if (pp->pos < pp->src_len && pp->src[pp->pos] == ':') pp->pos++;
        skip_ws(pp);

        // scan_string keeps the surrounding quotes; the key is shown and pathed without them
        size_t klen = strlen(key);
        if (klen >= 2 && key[0] == '"' && key[klen - 1] == '"') {
            memmove(key, key + 1, klen - 2);
            key[klen - 2] = '\0';
        }

        size_t mark = path_push(pp, key);
        int pos_before = pp->pos;
        format_value(pp, key);
        if (pp->pos == pos_before) pp->pos++; // malformed input: always make progress
        path_pop(pp, mark);
        skip_ws(pp);

        if (pp->pos < pp->src_len && pp->src[pp->pos] == ',') {
            pp->pos++;
            mark_trailing_comma(pp);
        }
        skip_ws(pp);
    }
    if (pp->pos < pp->src_len && pp->src[pp->pos] == '}') pp->pos++;
}

static void format_array_contents(JsonPP* pp) {
    skip_ws(pp);
    int idx = 0;
    while (pp->pos < pp->src_len && pp->src[pp->pos] != ']' && pp->line_count < JSON_PP_MAX_LINES) {
        skip_ws(pp);
        if (pp->pos >= pp->src_len || pp->src[pp->pos] == ']') break;

        char idx_buf[16];
        snprintf(idx_buf, sizeof(idx_buf), "%d", idx);
        size_t mark = path_push(pp, idx_buf);
        int pos_before = pp->pos;
        format_value(pp, "");
        if (pp->pos == pos_before) pp->pos++;
        path_pop(pp, mark);
        idx++;
        skip_ws(pp);

        if (pp->pos < pp->src_len && pp->src[pp->pos] == ',') {
            pp->pos++;
            mark_trailing_comma(pp);
        }
        skip_ws(pp);
    }
    if (pp->pos < pp->src_len && pp->src[pp->pos] == ']') pp->pos++;
}

static bool quoted_is_numeric(const char* val) {
    size_t vlen = strlen(val);
    if (vlen < 2 || val[0] != '"' || val[vlen - 1] != '"') return false;

    char inner[JSON_PP_VAL_LEN];
    size_t ilen = vlen - 2;
    if (ilen >= sizeof(inner)) ilen = sizeof(inner) - 1;
    memcpy(inner, val + 1, ilen);
    inner[ilen] = '\0';
    char* endp = NULL;
    double dv = strtod(inner, &endp);
    return endp != inner && *endp == '\0' && isfinite(dv);
}

static void format_value(JsonPP* pp, const char* key) {
    skip_ws(pp);
    if (pp->pos >= pp->src_len || pp->line_count >= JSON_PP_MAX_LINES) return;
    if (pp->depth >= JSON_PP_MAX_DEPTH) return;

    char c = pp->src[pp->pos];
    if (c == '{') {
        emit_line(pp, key, "{", JSON_PP_VAL_PUNCT, false);
        pp->pos++;
        pp->depth++;
        format_object_contents(pp);
        pp->depth--;
        emit_line(pp, "", "}", JSON_PP_VAL_PUNCT, false);
    } else if (c == '[') {
        emit_line(pp, key, "[", JSON_PP_VAL_PUNCT, false);
        pp->pos++;
        pp->depth++;
        format_array_contents(pp);
        pp->depth--;
        emit_line(pp, "", "]", JSON_PP_VAL_PUNCT, false);
    } else if (c == '"') {
        char val[JSON_PP_VAL_LEN] = "";
        scan_string(pp, val, sizeof(val));
        emit_line(pp, key, val, JSON_PP_VAL_STRING, quoted_is_numeric(val));
    } else {
        char val[JSON_PP_VAL_LEN] = "";
        scan_atom(pp, val, sizeof(val));
        char* endp = NULL;
        double dv = strtod(val, &endp);
        emit_line(pp, key, val, JSON_PP_VAL_ATOM, endp != val && isfinite(dv));
    }
}

bool json_pp_looks_like_json(const char* src) {
    if (!src || src[0] == '\0') return false;
    if (src[0] == '{' || src[0] == '[' || src[0] == '"') return true;

    // bare number, optionally surrounded by whitespace; hex is not JSON
    const char* p = src;
    while (*p == ' ') p++;
    if (*p == '+' || *p == '-') p++;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) return false;
    char* end = NULL;
    double v = strtod(src, &end);
    if (end == src) return false;
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') end++;
    return *end == '\0' && isfinite(v);
}

void json_pp_run(JsonPP* pp, const char* src) {
    json_pp_run_len(pp, src, strlen(src));
}

void json_pp_run_len(JsonPP* pp, const char* src, size_t len) {
    pp->src = src;
    pp->src_len = len > (size_t)INT32_MAX ? INT32_MAX : (int)len;
    pp->pos = 0;
    pp->depth = 0;
    pp->path_len = 0;
    pp->path[0] = '\0';
    pp->line_count = 0;
    pp->cache_src = NULL;
    format_value(pp, "");
    pp->src = NULL; // cursor is dead outside a run
}

bool json_pp_run_cached(JsonPP* pp, const char* src, uint64_t key) {
    if (pp->cache_src == src && pp->cache_key == key) return false;
    json_pp_run(pp, src);
    json_pp_mark_cached(pp, src, key);
    return true;
}

void json_pp_mark_cached(JsonPP* pp, const char* src, uint64_t key) {
    pp->cache_src = src;
    pp->cache_key = key;
}

const JsonPPLine* json_pp_find(const JsonPP* pp, const char* dot_path) {
    for (int i = 0; i < pp->line_count; i++) {
        if (strcmp(pp->lines[i].dot_path, dot_path) == 0) return &pp->lines[i];
    }
    return NULL;
}

bool json_pp_line_number(const JsonPPLine* line, double* out) {
    if (!line || !line->is_numeric) return false;
    const char* text = line->val;
    char inner[JSON_PP_VAL_LEN];
    if (line->val_kind == JSON_PP_VAL_STRING) {
        if (!json_pp_line_string(line, inner, sizeof(inner))) return false;
        text = inner;
    }
    char* end = NULL;
    double v = strtod(text, &end);
    if (end == text || !isfinite(v)) return false;
    *out = v;
    return true;
}

bool json_pp_number_at(const JsonPP* pp, const char* dot_path, double* out) {
    const JsonPPLine* line = json_pp_find(pp, dot_path);
    if (line) return json_pp_line_number(line, out);
    // path missing: a payload that is nothing but one scalar has line_count == 1 with dot_path ""
    if (pp->line_count == 1 && pp->lines[0].dot_path[0] == '\0') return json_pp_line_number(&pp->lines[0], out);
    return false;
}

bool json_pp_line_string(const JsonPPLine* line, char* out, size_t cap) {
    if (!line || cap == 0 || line->val_kind != JSON_PP_VAL_STRING) return false;
    size_t vlen = strlen(line->val);
    const char* p = line->val + 1;
    const char* end = line->val + vlen - (vlen >= 2 && line->val[vlen - 1] == '"' ? 1 : 0);

    size_t o = 0;
    while (p < end && o < cap - 1) {
        char ch = *p++;
        if (ch == '\\' && p < end) {
            char e = *p++;
            switch (e) {
                case 'n':
                    ch = '\n';
                    break;
                case 'r':
                    ch = '\r';
                    break;
                case 't':
                    ch = '\t';
                    break;
                case '"':
                case '\\':
                case '/':
                    ch = e;
                    break;
                default:
                    // unknown escape (incl. \uXXXX): keep it verbatim
                    out[o++] = '\\';
                    if (o >= cap - 1) break;
                    ch = e;
                    break;
            }
        }
        out[o++] = ch;
    }
    out[o] = '\0';
    return true;
}
