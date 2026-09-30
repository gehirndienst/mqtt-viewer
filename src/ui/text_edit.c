// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "raylib.h"

#include "platform/ui.h"
#include "ui/text_edit.h"
#include "ui/text_input.h"

#define TEXT_EDIT_SPACING 1.0f // must match measure_text() in platform/ui.c
#define TEXT_EDIT_MARKER '|'

static bool is_cont(unsigned char c) {
    return (c & 0xC0) == 0x80;
}

static size_t prev_boundary(const char* s, size_t i) {
    if (i == 0) return 0;
    i--;
    while (i > 0 && is_cont((unsigned char)s[i])) i--;
    return i;
}

static size_t next_boundary(const char* s, size_t len, size_t i) {
    if (i >= len) return len;
    i++;
    while (i < len && is_cont((unsigned char)s[i])) i++;
    return i;
}

static bool mod_down(void) {
    return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) || IsKeyDown(KEY_LEFT_SUPER) ||
        IsKeyDown(KEY_RIGHT_SUPER);
}

static bool key_hit(int key) {
    return IsKeyPressed(key) || IsKeyPressedRepeat(key);
}

static size_t insert_at(char* buf, size_t cap, size_t at, const char* src, size_t n) {
    size_t len = strlen(buf);
    if (n == 0 || len + n + 1 > cap) return 0;
    memmove(buf + at + n, buf + at, len - at + 1);
    memcpy(buf + at, src, n);
    return n;
}

bool text_edit_update(char* buf, size_t cap, size_t* caret, bool* all_selected, bool allow_newlines, bool ascii_only) {
    if (!buf || cap == 0) return false;
    bool changed = false;
    size_t len = strlen(buf);
    if (*caret > len) *caret = len;

    if (text_input_select_all_pressed()) *all_selected = true;
    if (text_input_handle_copy(buf)) return false;

    int ch;
    while ((ch = GetCharPressed()) != 0) {
        if (ch < 32 || ch == 127 || (ascii_only && ch > 126)) continue;
        if (*all_selected) {
            buf[0] = '\0';
            *caret = 0;
            *all_selected = false;
        }
        int n = 0;
        const char* utf8 = CodepointToUTF8(ch, &n);
        *caret += insert_at(buf, cap, *caret, utf8, (size_t)n);
        changed = true;
    }

    if (text_input_paste_pressed()) {
        static char s_paste[8192];
        s_paste[0] = '\0';
        text_input_append_filtered(s_paste, sizeof(s_paste), GetClipboardText(), allow_newlines);
        if (*all_selected) {
            buf[0] = '\0';
            *caret = 0;
            *all_selected = false;
        }
        size_t room = cap - strlen(buf) - 1;
        size_t n = strlen(s_paste);
        if (n > room) {
            n = room;
            while (n > 0 && is_cont((unsigned char)s_paste[n])) n--; // do not cut a UTF-8 sequence
        }
        *caret += insert_at(buf, cap, *caret, s_paste, n);
        changed = true;
    }

    bool mod = mod_down();
    if (key_hit(KEY_BACKSPACE)) {
        if (*all_selected) {
            buf[0] = '\0';
            *caret = 0;
        } else if (*caret > 0) {
            size_t from = mod ? 0 : prev_boundary(buf, *caret);
            memmove(buf + from, buf + *caret, strlen(buf) - *caret + 1);
            *caret = from;
        }
        *all_selected = false;
        changed = true;
    }
    if (key_hit(KEY_DELETE)) {
        len = strlen(buf);
        if (*all_selected) {
            buf[0] = '\0';
            *caret = 0;
        } else if (*caret < len) {
            size_t to = mod ? len : next_boundary(buf, len, *caret);
            memmove(buf + *caret, buf + to, len - to + 1);
        }
        *all_selected = false;
        changed = true;
    }

    len = strlen(buf);
    bool moved = false;
    if (key_hit(KEY_LEFT)) {
        *caret = (*all_selected || mod) ? 0 : prev_boundary(buf, *caret);
        moved = true;
    } else if (key_hit(KEY_RIGHT)) {
        *caret = (*all_selected || mod) ? len : next_boundary(buf, len, *caret);
        moved = true;
    } else if (key_hit(KEY_HOME)) {
        *caret = 0;
        moved = true;
    } else if (key_hit(KEY_END)) {
        *caret = len;
        moved = true;
    }
    if (moved) *all_selected = false;

    return changed;
}

size_t text_edit_format(char* out, size_t out_cap, const char* raw, size_t caret, bool show_marker) {
    if (out_cap < 6) {
        if (out_cap > 0) out[0] = '\0';
        return TEXT_EDIT_NO_MARKER;
    }
    if (!raw) raw = "";
    size_t len = strlen(raw);
    size_t room = out_cap - 2; // marker + NUL
    bool cut = len > room;
    size_t shown = len;
    if (cut) {
        shown = room - 3; // "..."
        while (shown > 0 && is_cont((unsigned char)raw[shown])) shown--;
    }
    size_t at = caret < shown ? caret : shown;
    size_t pos = 0;
    memcpy(out, raw, at);
    pos = at;
    size_t marker = TEXT_EDIT_NO_MARKER;
    if (show_marker) {
        marker = pos;
        out[pos++] = TEXT_EDIT_MARKER;
    }
    memcpy(out + pos, raw + at, shown - at);
    pos += shown - at;
    if (cut) {
        memcpy(out + pos, "...", 3);
        pos += 3;
    }
    out[pos] = '\0';
    return marker;
}

static float measure_range(Font font, const char* s, size_t from, size_t to, float size) {
    static char buf[4096];
    size_t n = to - from;
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    memcpy(buf, s + from, n);
    buf[n] = '\0';
    return MeasureTextEx(font, buf, size, TEXT_EDIT_SPACING).x;
}

size_t text_edit_hit_test(const char* display, size_t marker, int font_id, float font_size, float wrap_width,
                          float rel_x, float rel_y) {
    Font font = ui_get_font(font_id);
    size_t dlen = strlen(display);

    int target_line = rel_y > 0 ? (int)(rel_y / font_size) : 0;
    size_t ls = 0;
    size_t le = dlen;
    int line = 0;
    size_t i = 0;
    while (true) {
        size_t hard = i;
        while (hard < dlen && display[hard] != '\n') hard++;
        size_t end = hard;
        if (wrap_width > 0 && measure_range(font, display, i, hard, font_size) > wrap_width) {
            size_t last_fit = i;
            size_t w = i;
            while (w < hard) {
                size_t sp = w;
                while (sp < hard && display[sp] != ' ') sp++;
                if (sp < hard) sp++; // the space stays on this line
                if (measure_range(font, display, i, sp, font_size) > wrap_width && last_fit > i) break;
                last_fit = sp;
                w = sp;
            }
            end = last_fit > i ? last_fit : hard;
        }
        ls = i;
        le = end;
        if (line == target_line || end >= dlen) break;
        line++;
        i = end < hard ? end : hard + 1; // skip the '\n' itself
    }

    // Nearest character boundary on that line
    size_t best = ls;
    float best_d = 1e30f;
    for (size_t b = ls; b <= le; b = next_boundary(display, dlen, b)) {
        float x = measure_range(font, display, ls, b, font_size);
        float d = x > rel_x ? x - rel_x : rel_x - x;
        if (d < best_d) {
            best_d = d;
            best = b;
        }
        if (b >= le) break;
    }

    if (marker == TEXT_EDIT_NO_MARKER || best <= marker) return best;
    return best - 1;
}
