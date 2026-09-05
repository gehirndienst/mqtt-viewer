// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "clay.h"
#include "raylib.h"

#include "model/message_buf.h"
#include "model/util.h"
#include "ui/inspector_widget.h"
#include "ui/theme.h"
#include "ui/ui_util.h"

#define DIFF_MAX_LINES 256
#define DIFF_LINE_LEN 192
#define HISTORY_MAX_ROWS 200
#define JSON_PP_MAX_LINES 2048
#define JSON_PP_MAX_DEPTH 64
#define JSON_INDENT_STEP 16
#define JSON_PP_KEY_LEN 128
#define JSON_PP_VAL_LEN 200
#define PP_GUTTER_W 18

typedef enum {
    DIFF_UNCHANGED,
    DIFF_ADDED,
    DIFF_REMOVED,
} DiffState;

typedef struct {
    int depth;
    char key[JSON_PP_KEY_LEN];
    char sep[4]; // ": " or ""
    char val[JSON_PP_VAL_LEN];
    char trail[4]; // "," or ""
    Clay_Color key_color;
    Clay_Color val_color;
    char dot_path[CHART_DOT_PATH_LEN]; // path of this line's leaf for chart-add ("" = root)
    bool is_numeric;
} JsonPPLine;

typedef struct {
    int curr_idx; // index into s_pp_lines (-1 for REMOVED-only entries)
    int prev_idx; // index into s_diff_prev_text (-1 unless REMOVED or UNCHANGED)
    DiffState state;
} DiffMergedEntry;

static JsonPPLine s_pp_lines[JSON_PP_MAX_LINES];
static int s_pp_line_count;
static const char* s_pp_src;
static int s_pp_src_len;
static int s_pp_pos;
static int s_pp_depth;
static char s_pp_path[CHART_DOT_PATH_LEN];
static size_t s_pp_path_len;

// action button flash

static int s_copied_btn = -1;
static float s_copied_timer = 0.0f;

// Strategy: line-wise LCS over the JSON pp output. The diff baseline is the previous payload preview;
// when a new message arrives reprint the previous preview build line-text reps for both previous and current run an LCS
// to mark each current line as UNCHANGED/ADDED and emit synthetic REMOVED rows for prev-only lines
static char s_diff_prev_preview[TOPIC_PREVIEW_LEN];
static const TopicNode* s_diff_prev_node = NULL;
static uint64_t s_diff_prev_ts = 0;
static char s_diff_prev_text[DIFF_MAX_LINES][DIFF_LINE_LEN];
static int s_diff_prev_text_depth[DIFF_MAX_LINES];
static int s_diff_prev_text_count = 0;
static char s_diff_curr_text[DIFF_MAX_LINES][DIFF_LINE_LEN];
static DiffMergedEntry s_diff_merged[DIFF_MAX_LINES * 2];
static int s_diff_merged_count = 0;
static int s_diff_dp[DIFF_MAX_LINES + 1][DIFF_MAX_LINES + 1];

// temporary buffer to swap s_pp_lines while we re-pp the previous preview without losing the current frame's pp output
static JsonPPLine s_diff_pp_swap[JSON_PP_MAX_LINES];

// history view
static char s_hist_time_bufs[HISTORY_MAX_ROWS][16];
static char s_hist_meta_bufs[HISTORY_MAX_ROWS][32];
// by timestamp (not index) so new messages don't shift state
static uint64_t s_hist_copied_ts = 0;
static float s_hist_copied_timer = 0.0f;
static uint64_t s_hist_expanded_ts = 0;
static TopicNode* s_last_hist_node = NULL;

// mutual recursion between pp_format_* functions
static void pp_format_value(const char* key, Clay_Color key_color);

static void render_text_view(const char* src) {
    if (src[0] == '\0') {
        CLAY_TEXT(CLAY_STRING("(no payload)"), THEME_TEXT_SMALL);
        return;
    }
    int total_len = (int)strlen(src);
    int display_len = total_len > 4000 ? 4000 : total_len;
    Clay_String ts = {.length = display_len, .chars = src};
    CLAY_TEXT(ts, THEME_TEXT_MONO);
    if (total_len > 4000) {
        CLAY_TEXT(CLAY_STRING("... (use JSON tab for large payloads)"), THEME_TEXT_SMALL);
    }
}

// hexdump line widthfits in < 80 chars
#define HEX_LINE_BUF 80

// Format a single 16-byte hex-dump line ("offset  hex bytes  |ascii|") into buf without a trailing newline
static int format_hex_line(char* buf, int buf_size, const char* src, int len, int offset) {
    int n = len - offset;
    if (n > 16) n = 16;
    int pos = 0;
    pos += snprintf(buf + pos, buf_size - pos, "%08x  ", offset);
    for (int j = 0; j < 16; j++) {
        if (j < n)
            pos += snprintf(buf + pos, buf_size - pos, "%02x ", (unsigned char)src[offset + j]);
        else
            pos += snprintf(buf + pos, buf_size - pos, "   ");
        if (j == 7) pos += snprintf(buf + pos, buf_size - pos, " ");
    }
    pos += snprintf(buf + pos, buf_size - pos, " |");
    for (int j = 0; j < n && pos < buf_size - 2; j++) {
        unsigned char c = (unsigned char)src[offset + j];
        buf[pos++] = (c >= 0x20 && c < 0x7f) ? (char)c : '.';
    }
    buf[pos++] = '|';
    buf[pos] = '\0';
    return pos;
}

static void render_hex_view(const char* src) {
    int len = (int)strlen(src);
    if (len == 0) {
        CLAY_TEXT(CLAY_STRING("(no payload)"), THEME_TEXT_SMALL);
        return;
    }

    // cap at 64 lines (1024 bytes shown)
    static char hex_lines[64][HEX_LINE_BUF];
    int line_count = 0;
    for (int offset = 0; offset < len && line_count < 64; offset += 16) {
        format_hex_line(hex_lines[line_count], HEX_LINE_BUF, src, len, offset);
        line_count++;
    }

    for (int i = 0; i < line_count; i++) {
        Clay_String hs = ui_utils_clay_string(hex_lines[i]);
        CLAY_TEXT(hs, THEME_TEXT_MONO);
    }
}

static void build_hex_dump_str(const char* src, int len, char* out, int out_size) {
    int pos = 0;
    char line[HEX_LINE_BUF];
    for (int offset = 0; offset < len; offset += 16) {
        int n = format_hex_line(line, sizeof(line), src, len, offset);
        if (pos + n + 1 >= out_size) break; // leave room for '\n' and terminator
        memcpy(out + pos, line, (size_t)n);
        pos += n;
        out[pos++] = '\n';
    }
    if (pos > 0 && out[pos - 1] == '\n') pos--;
    out[pos] = '\0';
}

static void pp_skip_ws(void) {
    while (s_pp_pos < s_pp_src_len &&
           (s_pp_src[s_pp_pos] == ' ' || s_pp_src[s_pp_pos] == '\t' || s_pp_src[s_pp_pos] == '\n' ||
            s_pp_src[s_pp_pos] == '\r'))
        s_pp_pos++;
}

static void pp_scan_string(char* buf, int buf_size) {
    int start = s_pp_pos;
    s_pp_pos++;
    while (s_pp_pos < s_pp_src_len) {
        if (s_pp_src[s_pp_pos] == '\\' && s_pp_pos + 1 < s_pp_src_len) {
            s_pp_pos += 2;
            continue;
        }
        if (s_pp_src[s_pp_pos] == '"') {
            s_pp_pos++;
            break;
        }
        s_pp_pos++;
    }
    int copy_len = s_pp_pos - start;
    if (copy_len > buf_size - 1) copy_len = buf_size - 1;
    memcpy(buf, s_pp_src + start, (size_t)copy_len);
    buf[copy_len] = '\0';
}

static void pp_scan_atom(char* buf, int buf_size) {
    int start = s_pp_pos;
    while (s_pp_pos < s_pp_src_len) {
        char c = s_pp_src[s_pp_pos];
        if (c == ',' || c == '}' || c == ']' || c == '{' || c == '[' || c == ' ' || c == '\t' || c == '\n' || c == '\r')
            break;
        s_pp_pos++;
    }
    int copy_len = s_pp_pos - start;
    if (copy_len > buf_size - 1) copy_len = buf_size - 1;
    memcpy(buf, s_pp_src + start, (size_t)copy_len);
    buf[copy_len] = '\0';
}

static size_t pp_path_push(const char* seg) {
    size_t mark = s_pp_path_len;
    if (!seg || !seg[0]) return mark;
    size_t need_dot = (s_pp_path_len > 0) ? 1 : 0;
    size_t seg_len = strlen(seg);
    if (s_pp_path_len + need_dot + seg_len + 1 > sizeof(s_pp_path)) return mark;
    if (need_dot) s_pp_path[s_pp_path_len++] = '.';
    memcpy(s_pp_path + s_pp_path_len, seg, seg_len);
    s_pp_path_len += seg_len;
    s_pp_path[s_pp_path_len] = '\0';
    return mark;
}

static void pp_path_pop(size_t mark) {
    s_pp_path_len = mark;
    s_pp_path[s_pp_path_len] = '\0';
}

static void pp_emit_line(const char* key, Clay_Color key_color, const char* sep, const char* val, Clay_Color val_color,
                         bool is_numeric) {
    if (s_pp_line_count >= JSON_PP_MAX_LINES) return;
    JsonPPLine* line = &s_pp_lines[s_pp_line_count++];
    line->depth = s_pp_depth;
    util_str_copy(line->key, JSON_PP_KEY_LEN, key);
    util_str_copy(line->sep, sizeof(line->sep), sep);
    util_str_copy(line->val, JSON_PP_VAL_LEN, val);
    line->trail[0] = '\0';
    line->key_color = key_color;
    line->val_color = val_color;
    line->is_numeric = is_numeric;
    size_t plen = s_pp_path_len < sizeof(line->dot_path) - 1 ? s_pp_path_len : sizeof(line->dot_path) - 1;
    memcpy(line->dot_path, s_pp_path, plen);
    line->dot_path[plen] = '\0';
}

static void pp_format_object_contents(void) {
    pp_skip_ws();
    while (s_pp_pos < s_pp_src_len && s_pp_src[s_pp_pos] != '}' && s_pp_line_count < JSON_PP_MAX_LINES) {
        pp_skip_ws();
        if (s_pp_pos >= s_pp_src_len || s_pp_src[s_pp_pos] == '}') break;

        char key[JSON_PP_KEY_LEN] = "";
        if (s_pp_src[s_pp_pos] == '"') pp_scan_string(key, sizeof(key));
        pp_skip_ws();
        if (s_pp_pos < s_pp_src_len && s_pp_src[s_pp_pos] == ':') s_pp_pos++;
        pp_skip_ws();

        // pp_scan_string keeps the surrounding quotes; strip them for the path
        size_t klen = strlen(key);
        if (klen >= 2 && key[0] == '"' && key[klen - 1] == '"') {
            memmove(key, key + 1, klen - 2);
            key[klen - 2] = '\0';
        }

        size_t mark = pp_path_push(key);
        int pos_before = s_pp_pos;
        pp_format_value(key, THEME_LIGHT_BLUE);
        // Skip one char on malformed JSON to prevent an infinite loop
        if (s_pp_pos == pos_before) s_pp_pos++;
        pp_path_pop(mark);
        pp_skip_ws();

        if (s_pp_pos < s_pp_src_len && s_pp_src[s_pp_pos] == ',') {
            s_pp_pos++;
            if (s_pp_line_count > 0)
                util_str_copy(s_pp_lines[s_pp_line_count - 1].trail, sizeof(s_pp_lines[0].trail), ",");
        }
        pp_skip_ws();
    }
    if (s_pp_pos < s_pp_src_len && s_pp_src[s_pp_pos] == '}') s_pp_pos++;
}

static void pp_format_array_contents(void) {
    pp_skip_ws();
    int idx = 0;
    while (s_pp_pos < s_pp_src_len && s_pp_src[s_pp_pos] != ']' && s_pp_line_count < JSON_PP_MAX_LINES) {
        pp_skip_ws();
        if (s_pp_pos >= s_pp_src_len || s_pp_src[s_pp_pos] == ']') break;

        char idx_buf[16];
        snprintf(idx_buf, sizeof(idx_buf), "%d", idx);
        size_t mark = pp_path_push(idx_buf);
        int pos_before = s_pp_pos;
        pp_format_value("", THEME_TEXT_MUTED);
        if (s_pp_pos == pos_before) s_pp_pos++;
        pp_path_pop(mark);
        idx++;
        pp_skip_ws();

        if (s_pp_pos < s_pp_src_len && s_pp_src[s_pp_pos] == ',') {
            s_pp_pos++;
            if (s_pp_line_count > 0)
                util_str_copy(s_pp_lines[s_pp_line_count - 1].trail, sizeof(s_pp_lines[0].trail), ",");
        }
        pp_skip_ws();
    }
    if (s_pp_pos < s_pp_src_len && s_pp_src[s_pp_pos] == ']') s_pp_pos++;
}

static void pp_format_value(const char* key, Clay_Color key_color) {
    pp_skip_ws();
    if (s_pp_pos >= s_pp_src_len || s_pp_line_count >= JSON_PP_MAX_LINES) return;
    if (s_pp_depth >= JSON_PP_MAX_DEPTH) return;

    char c = s_pp_src[s_pp_pos];
    if (c == '{') {
        pp_emit_line(key, key_color, key[0] ? ": " : "", "{", THEME_TEXT_MUTED, false);
        s_pp_pos++;
        s_pp_depth++;
        pp_format_object_contents();
        s_pp_depth--;
        pp_emit_line("", THEME_TEXT_MUTED, "", "}", THEME_TEXT_MUTED, false);
    } else if (c == '[') {
        pp_emit_line(key, key_color, key[0] ? ": " : "", "[", THEME_TEXT_MUTED, false);
        s_pp_pos++;
        s_pp_depth++;
        pp_format_array_contents();
        s_pp_depth--;
        pp_emit_line("", THEME_TEXT_MUTED, "", "]", THEME_TEXT_MUTED, false);
    } else if (c == '"') {
        char val[JSON_PP_VAL_LEN] = "";
        pp_scan_string(val, sizeof(val));
        // Strings that *look* like numbers
        bool is_num_str = false;
        size_t vlen = strlen(val);
        if (vlen >= 2 && val[0] == '"' && val[vlen - 1] == '"') {
            char inner[JSON_PP_VAL_LEN];
            size_t ilen = vlen - 2;
            if (ilen >= sizeof(inner)) ilen = sizeof(inner) - 1;
            memcpy(inner, val + 1, ilen);
            inner[ilen] = '\0';
            char* endp = NULL;
            double dv = strtod(inner, &endp);
            is_num_str = (endp != inner) && (*endp == '\0') && isfinite(dv);
            (void)dv;
        }
        pp_emit_line(key, key_color, key[0] ? ": " : "", val, THEME_GREEN, is_num_str);
    } else {
        char val[JSON_PP_VAL_LEN] = "";
        pp_scan_atom(val, sizeof(val));
        // Detect numeric atoms
        char* endp = NULL;
        double dv = strtod(val, &endp);
        bool is_num = (endp != val) && isfinite(dv);
        (void)dv;
        pp_emit_line(key, key_color, key[0] ? ": " : "", val, THEME_PINK, is_num);
    }
}

static char s_inspector_topic[CHART_TOPIC_LEN]; // updated each frame from selected_topic

void inspector_chart_add_from_line(AppState* state, int line_idx) {
    if (line_idx < 0 || line_idx >= s_pp_line_count) return;
    if (s_inspector_topic[0] == '\0') return;
    const JsonPPLine* line = &s_pp_lines[line_idx];
    if (!line->is_numeric) return;
    // skip if (topic, dot_path) already active
    for (int i = 0; i < CHART_MAX_SERIES; i++) {
        ChartSeries* s = &state->chart_series[i];
        if (s->active && strcmp(s->topic, s_inspector_topic) == 0 && strcmp(s->dot_path, line->dot_path) == 0) {
            return;
        }
    }
    for (int i = 0; i < CHART_MAX_SERIES; i++) {
        if (!state->chart_series[i].active) {
            chart_series_init(&state->chart_series[i], s_inspector_topic, line->dot_path);
            return;
        }
    }
}

// Stable text identity for a pretty-printed line: key + sep + val + trail
static void diff_format_line(const JsonPPLine* line, char* out) {
    snprintf(out, DIFF_LINE_LEN, "%s%s%s%s", line->key, line->sep, line->val, line->trail);
}

static bool diff_lines_equal(int prev_idx, int curr_idx, const int* curr_depth) {
    if (s_diff_prev_text_depth[prev_idx] != curr_depth[curr_idx]) return false;
    return strcmp(s_diff_prev_text[prev_idx], s_diff_curr_text[curr_idx]) == 0;
}

// LCS-based line diff
static void diff_compute(int prev_n, int curr_n, const int* curr_depth) {
    if (prev_n > DIFF_MAX_LINES) prev_n = DIFF_MAX_LINES;
    if (curr_n > DIFF_MAX_LINES) curr_n = DIFF_MAX_LINES;

    for (int i = 0; i <= prev_n; i++) s_diff_dp[i][0] = 0;
    for (int j = 0; j <= curr_n; j++) s_diff_dp[0][j] = 0;
    for (int i = 1; i <= prev_n; i++) {
        for (int j = 1; j <= curr_n; j++) {
            if (diff_lines_equal(i - 1, j - 1, curr_depth)) {
                s_diff_dp[i][j] = s_diff_dp[i - 1][j - 1] + 1;
            } else {
                int a = s_diff_dp[i - 1][j];
                int b = s_diff_dp[i][j - 1];
                s_diff_dp[i][j] = a > b ? a : b;
            }
        }
    }

    static DiffMergedEntry tmp[DIFF_MAX_LINES * 2];
    int tc = 0;
    int i = prev_n, j = curr_n;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && diff_lines_equal(i - 1, j - 1, curr_depth)) {
            tmp[tc++] = (DiffMergedEntry){.curr_idx = j - 1, .prev_idx = i - 1, .state = DIFF_UNCHANGED};
            i--;
            j--;
        } else if (j > 0 && (i == 0 || s_diff_dp[i][j - 1] >= s_diff_dp[i - 1][j])) {
            tmp[tc++] = (DiffMergedEntry){.curr_idx = j - 1, .prev_idx = -1, .state = DIFF_ADDED};
            j--;
        } else {
            tmp[tc++] = (DiffMergedEntry){.curr_idx = -1, .prev_idx = i - 1, .state = DIFF_REMOVED};
            i--;
        }
    }

    s_diff_merged_count = tc;
    for (int k = 0; k < tc; k++) s_diff_merged[k] = tmp[tc - 1 - k];
}

static void render_pp_line(const JsonPPLine* line, int line_idx, DiffState st) {
    uint16_t left_pad = (uint16_t)(line->depth * JSON_INDENT_STEP);

    Clay_Color key_c = (st == DIFF_ADDED) ? THEME_DIFF_ADDED : line->key_color;
    Clay_Color sep_c = (st == DIFF_ADDED) ? THEME_DIFF_ADDED : THEME_TEXT_MUTED;
    Clay_Color val_c = (st == DIFF_ADDED) ? THEME_DIFF_ADDED : line->val_color;
    Clay_Color trail_c = (st == DIFF_ADDED) ? THEME_DIFF_ADDED : THEME_TEXT_MUTED;
    Clay_Color content_bg = (st == DIFF_ADDED) ? THEME_DIFF_BG_ADDED : (Clay_Color){0};

    CLAY(CLAY_IDI("PP", (uint32_t)line_idx),
         {
             .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                        .childGap = 0,
                        .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
         }) {
        // Column 1: gutter
        CLAY(CLAY_IDI("PPGut", (uint32_t)line_idx),
             {
                 .layout = {.sizing = {CLAY_SIZING_FIXED(PP_GUTTER_W), CLAY_SIZING_FIT(0)},
                            .padding = {0, 0, 1, 1},
                            .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
                 .border = {.width = {.right = 1}, .color = THEME_BORDER},
             }) {
            if (line->is_numeric) {
                CLAY(CLAY_IDI("ChartAdd", (uint32_t)line_idx),
                     {
                         .layout = {.sizing = {CLAY_SIZING_FIXED(13), CLAY_SIZING_FIXED(13)},
                                    .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
                     }) {
                    CLAY_TEXT(CLAY_STRING("+"),
                              CLAY_TEXT_CONFIG({.fontSize = 11, .fontId = FONT_DEFAULT, .textColor = THEME_GREEN}));
                }
            }
        }
        // Column 2: content
        CLAY(CLAY_IDI("PPCon", (uint32_t)line_idx),
             {
                 .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                            .padding = {(uint16_t)(left_pad + 6), 0, 0, 0},
                            .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
                 .backgroundColor = content_bg,
             }) {
            if (line->key[0]) {
                Clay_String ks = ui_utils_clay_string(line->key);
                CLAY_TEXT(
                    ks,
                    CLAY_TEXT_CONFIG(
                        {.fontSize = 13, .fontId = FONT_MONO, .textColor = key_c, .wrapMode = CLAY_TEXT_WRAP_NONE}));
            }
            if (line->sep[0]) {
                Clay_String ss = ui_utils_clay_string(line->sep);
                CLAY_TEXT(
                    ss,
                    CLAY_TEXT_CONFIG(
                        {.fontSize = 13, .fontId = FONT_MONO, .textColor = sep_c, .wrapMode = CLAY_TEXT_WRAP_NONE}));
            }
            if (line->val[0]) {
                Clay_String vs = ui_utils_clay_string(line->val);
                CLAY_TEXT(
                    vs,
                    CLAY_TEXT_CONFIG(
                        {.fontSize = 13, .fontId = FONT_MONO, .textColor = val_c, .wrapMode = CLAY_TEXT_WRAP_NONE}));
            }
            if (line->trail[0]) {
                Clay_String trs = ui_utils_clay_string(line->trail);
                CLAY_TEXT(
                    trs,
                    CLAY_TEXT_CONFIG(
                        {.fontSize = 13, .fontId = FONT_MONO, .textColor = trail_c, .wrapMode = CLAY_TEXT_WRAP_NONE}));
            }
        }
    }
}

// Render a synthetic REMOVED row from the previous frame's text
static void render_pp_removed(int merged_idx, int prev_idx) {
    uint16_t left_pad = (uint16_t)(s_diff_prev_text_depth[prev_idx] * JSON_INDENT_STEP);
    Clay_String txt = ui_utils_clay_string(s_diff_prev_text[prev_idx]);

    CLAY(CLAY_IDI("PPR", (uint32_t)merged_idx),
         {
             .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                        .childGap = 0,
                        .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
         }) {
        CLAY(CLAY_IDI("PPRGut", (uint32_t)merged_idx),
             {
                 .layout = {.sizing = {CLAY_SIZING_FIXED(PP_GUTTER_W), CLAY_SIZING_FIT(0)}},
                 .border = {.width = {.right = 1}, .color = THEME_BORDER},
             }) {}
        CLAY(CLAY_IDI("PPRCon", (uint32_t)merged_idx),
             {
                 .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                            .padding = {(uint16_t)(left_pad + 6), 0, 0, 0},
                            .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
                 .backgroundColor = THEME_DIFF_BG_REMOVED,
             }) {
            CLAY_TEXT(txt,
                      CLAY_TEXT_CONFIG({.fontSize = 13,
                                        .fontId = FONT_MONO,
                                        .textColor = THEME_DIFF_REMOVED,
                                        .wrapMode = CLAY_TEXT_WRAP_NONE}));
        }
    }
}

static bool payload_is_numeric_scalar(const char* src) {
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

static void render_json_diff_view(TopicNode* node) {
    const char* src = node->last_payload_preview;
    if (src[0] == '\0') {
        CLAY_TEXT(CLAY_STRING("(no payload)"), THEME_TEXT_SMALL);
        return;
    }
    if (src[0] != '{' && src[0] != '[' && src[0] != '"' && !payload_is_numeric_scalar(src)) {
        // Diff is JSON-only by design - fallback to plain text view for non-JSON payloads
        render_text_view(src);
        return;
    }

    s_pp_src = src;
    s_pp_src_len = (int)strlen(src);
    s_pp_pos = 0;
    s_pp_path_len = 0;
    s_pp_depth = 0;
    s_pp_line_count = 0;
    pp_format_value("", THEME_TEXT_MUTED);
    if (s_pp_line_count == 0) {
        render_text_view(src);
        return;
    }

    bool node_switched = (s_diff_prev_node != node);
    bool ts_advanced = (s_diff_prev_ts != node->last_message_ts);

    if (node_switched) {
        // First frame on this node - establish baseline, render plain
        s_diff_prev_node = node;
        s_diff_prev_ts = node->last_message_ts;
        size_t plen = strlen(src);
        if (plen >= sizeof(s_diff_prev_preview)) plen = sizeof(s_diff_prev_preview) - 1;
        memcpy(s_diff_prev_preview, src, plen);
        s_diff_prev_preview[plen] = '\0';
        s_diff_merged_count = 0;
        s_diff_prev_text_count = 0;
    } else if (ts_advanced) {
        int curr_n = s_pp_line_count > DIFF_MAX_LINES ? DIFF_MAX_LINES : s_pp_line_count;
        static int curr_depth[DIFF_MAX_LINES];
        for (int i = 0; i < curr_n; i++) {
            diff_format_line(&s_pp_lines[i], s_diff_curr_text[i]);
            curr_depth[i] = s_pp_lines[i].depth;
        }

        // Save current pretty-printer output, run PP on prev preview, capture its lines, then restore
        int saved_count = s_pp_line_count;
        memcpy(s_diff_pp_swap, s_pp_lines, (size_t)saved_count * sizeof(JsonPPLine));
        s_pp_src = s_diff_prev_preview;
        s_pp_src_len = (int)strlen(s_diff_prev_preview);
        s_pp_pos = 0;
        s_pp_path_len = 0;
        s_pp_depth = 0;
        s_pp_line_count = 0;
        pp_format_value("", THEME_TEXT_MUTED);
        int prev_n = s_pp_line_count > DIFF_MAX_LINES ? DIFF_MAX_LINES : s_pp_line_count;
        for (int i = 0; i < prev_n; i++) {
            diff_format_line(&s_pp_lines[i], s_diff_prev_text[i]);
            s_diff_prev_text_depth[i] = s_pp_lines[i].depth;
        }
        s_diff_prev_text_count = prev_n;
        memcpy(s_pp_lines, s_diff_pp_swap, (size_t)saved_count * sizeof(JsonPPLine));
        s_pp_line_count = saved_count;

        diff_compute(prev_n, curr_n, curr_depth);

        // Update baseline to the current preview
        size_t plen = strlen(src);
        if (plen >= sizeof(s_diff_prev_preview)) plen = sizeof(s_diff_prev_preview) - 1;
        memcpy(s_diff_prev_preview, src, plen);
        s_diff_prev_preview[plen] = '\0';
        s_diff_prev_ts = node->last_message_ts;
    }
    // else: cached merged sequence from prior frame remains valid

    // rndr
    if (s_diff_merged_count == 0) {
        for (int li = 0; li < s_pp_line_count; li++) {
            render_pp_line(&s_pp_lines[li], li, DIFF_UNCHANGED);
        }
    } else {
        for (int k = 0; k < s_diff_merged_count; k++) {
            DiffMergedEntry* e = &s_diff_merged[k];
            if (e->state == DIFF_REMOVED) {
                render_pp_removed(k, e->prev_idx);
            } else {
                render_pp_line(&s_pp_lines[e->curr_idx], e->curr_idx, e->state);
            }
        }
        for (int li = DIFF_MAX_LINES; li < s_pp_line_count; li++) {
            render_pp_line(&s_pp_lines[li], li, DIFF_UNCHANGED);
        }
    }
    if (s_pp_line_count >= JSON_PP_MAX_LINES) {
        CLAY_TEXT(CLAY_STRING("(output truncated at 2048 lines)"), THEME_TEXT_SMALL);
    }
}

static void render_json_view(const char* src) {
    if (src[0] == '\0') {
        CLAY_TEXT(CLAY_STRING("(no payload)"), THEME_TEXT_SMALL);
        return;
    }
    if (src[0] != '{' && src[0] != '[' && src[0] != '"' && !payload_is_numeric_scalar(src)) {
        render_text_view(src);
        return;
    }

    s_pp_src = src;
    s_pp_src_len = (int)strlen(src);
    s_pp_pos = 0;
    s_pp_path_len = 0;
    s_pp_depth = 0;
    s_pp_line_count = 0;
    pp_format_value("", THEME_TEXT_MUTED);

    if (s_pp_line_count == 0) {
        render_text_view(src);
        return;
    }

    for (int li = 0; li < s_pp_line_count; li++) {
        render_pp_line(&s_pp_lines[li], li, DIFF_UNCHANGED);
    }
    if (s_pp_line_count >= JSON_PP_MAX_LINES) {
        CLAY_TEXT(CLAY_STRING("(output truncated at 2048 lines)"), THEME_TEXT_SMALL);
    }
}

static void render_history_view(AppState* state, TopicNode* node) {
    // Advance copy-flash timer; clear when expired
    if (!ui_utils_flash_tick(&s_hist_copied_timer)) s_hist_copied_ts = 0;

    // Reset expanded row when the selected topic changes
    if (node != s_last_hist_node) {
        s_last_hist_node = node;
        s_hist_expanded_ts = 0;
    }

    static char hist_path[512];
    topic_node_full_path(node, hist_path, sizeof(hist_path));

    uint32_t total = message_buf_count(&state->global_history);
    if (total == 0) {
        CLAY_TEXT(CLAY_STRING("No history yet."), THEME_TEXT_SMALL);
        return;
    }

    static uint32_t matches[HISTORY_MAX_ROWS];
    int match_count = 0;
    for (int i = (int)total - 1; i >= 0 && match_count < HISTORY_MAX_ROWS; i--) {
        const MessageRecord* r = message_buf_get(&state->global_history, (uint32_t)i);
        if (r && strcmp(r->topic, hist_path) == 0) matches[match_count++] = (uint32_t)i;
    }

    if (match_count == 0) {
        CLAY_TEXT(CLAY_STRING("No messages for this topic yet."), THEME_TEXT_SMALL);
        return;
    }

    // Render rows
    for (int ri = 0; ri < match_count; ri++) {
        const MessageRecord* r = message_buf_get(&state->global_history, matches[ri]);
        if (!r) continue;

        bool expanded = (s_hist_expanded_ts != 0 && r->timestamp_us == s_hist_expanded_ts);
        bool copying = (s_hist_copied_ts != 0 && r->timestamp_us == s_hist_copied_ts);

        // Display-index-based Clay IDs (unique per frame even when timestamps collide)
        // Expand/copy state uses timestamps separately (s_hist_expanded_ts, s_hist_copied_ts)

        util_fmt_hhmmss(r->timestamp_us, s_hist_time_bufs[ri], sizeof(s_hist_time_bufs[ri]));
        snprintf(s_hist_meta_bufs[ri], sizeof(s_hist_meta_bufs[ri]), "Q%u  %u B%s", (unsigned)r->qos, r->payload_len,
                 r->retained ? "  R" : "");

        CLAY(CLAY_IDI("HR", (uint32_t)ri),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     },
                 .border = {.width = {.bottom = 1}, .color = THEME_BORDER_SUBTLE},
             }) {
            // hdr row: triangle + timestamp + meta + spacer + copy button
            CLAY(CLAY_IDI("HH", (uint32_t)ri),
                 {
                     .layout =
                         {
                             .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                             .padding = {6, 6, 5, 5},
                             .childGap = 8,
                             .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                         },
                     .backgroundColor = expanded ? THEME_BG_HISTORY_EXPANDED : (Clay_Color){0},
                 }) {
                CLAY_TEXT(expanded ? CLAY_STRING("\xe2\x96\xbe") : CLAY_STRING("\xe2\x96\xb8"),
                          CLAY_TEXT_CONFIG({
                              .fontSize = 10,
                              .fontId = FONT_DEFAULT,
                              .textColor = THEME_TEXT_DIM,
                          }));
                Clay_String ts_cs = ui_utils_clay_string(s_hist_time_bufs[ri]);
                CLAY_TEXT(ts_cs,
                          CLAY_TEXT_CONFIG({
                              .fontSize = 11,
                              .fontId = FONT_MONO,
                              .textColor = THEME_TEXT_DIM,
                          }));
                Clay_String ms_cs = ui_utils_clay_string(s_hist_meta_bufs[ri]);
                CLAY_TEXT(ms_cs,
                          CLAY_TEXT_CONFIG({
                              .fontSize = 11,
                              .fontId = FONT_MONO,
                              .textColor = THEME_TEXT_DIM,
                          }));
                CLAY(CLAY_IDI("HHS", (uint32_t)ri),
                     {
                         .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}},
                     }) {}
                CLAY(CLAY_IDI("HC", (uint32_t)ri), {
                    .layout = {.padding = {7, 7, 2, 2}},
                    .backgroundColor = copying ? THEME_BG_INPUT_ACTIVE : THEME_BG_BUTTON,
                    .cornerRadius = CLAY_CORNER_RADIUS(3),
                    .border = copying ? (Clay_BorderElementConfig){
                        .width = CLAY_BORDER_OUTSIDE(1), .color = THEME_ACCENT_BLUE,
                    }
                                      : (Clay_BorderElementConfig){0},
                }) {
                    CLAY_TEXT(copying ? CLAY_STRING("Copied!") : CLAY_STRING("Copy"),
                              CLAY_TEXT_CONFIG({
                                  .fontSize = 10,
                                  .fontId = FONT_DEFAULT,
                                  .textColor = copying ? THEME_ACCENT_BLUE : THEME_TEXT_MUTED,
                              }));
                }
            }

            // Expanded payload
            if (expanded && r->preview[0] != '\0') {
                CLAY(CLAY_IDI("HEC", (uint32_t)ri),
                     {
                         .layout =
                             {
                                 .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                                 .padding = {12, 12, 6, 8},
                                 .layoutDirection = CLAY_TOP_TO_BOTTOM,
                                 .childGap = 2,
                             },
                         .backgroundColor = THEME_BG_HISTORY_DETAIL,
                     }) {
                    const char* src = r->preview;
                    bool is_json = (src[0] == '{' || src[0] == '[' || src[0] == '"');
                    if (is_json) {
                        s_pp_src = src;
                        s_pp_src_len = (int)strlen(src);
                        s_pp_pos = 0;
                        s_pp_path_len = 0;
                        s_pp_depth = 0;
                        s_pp_line_count = 0;
                        pp_format_value("", THEME_TEXT_MUTED);
                    }
                    if (!is_json || s_pp_line_count == 0) {
                        int raw_len = (int)strlen(src);
                        if (raw_len > 4000) raw_len = 4000;
                        Clay_String raw_cs = {.length = raw_len, .chars = src};
                        CLAY_TEXT(raw_cs,
                                  CLAY_TEXT_CONFIG({
                                      .fontSize = 12,
                                      .fontId = FONT_MONO,
                                      .textColor = THEME_TEXT_SECONDARY,
                                  }));
                    } else {
                        for (int li = 0; li < s_pp_line_count; li++) {
                            JsonPPLine* line = &s_pp_lines[li];
                            uint16_t left_pad = (uint16_t)(line->depth * JSON_INDENT_STEP + 6);
                            CLAY(CLAY_IDI("EPP", (uint32_t)li),
                                 {
                                     .layout =
                                         {
                                             .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                                             .padding = {left_pad, 0, 0, 0},
                                             .childGap = 0,
                                             .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                                         },
                                 }) {
                                if (line->key[0]) {
                                    Clay_String ks = ui_utils_clay_string(line->key);
                                    CLAY_TEXT(ks,
                                              CLAY_TEXT_CONFIG({
                                                  .fontSize = 12,
                                                  .fontId = FONT_MONO,
                                                  .textColor = line->key_color,
                                                  .wrapMode = CLAY_TEXT_WRAP_NONE,
                                              }));
                                }
                                if (line->sep[0]) {
                                    Clay_String ss = ui_utils_clay_string(line->sep);
                                    CLAY_TEXT(ss,
                                              CLAY_TEXT_CONFIG({
                                                  .fontSize = 12,
                                                  .fontId = FONT_MONO,
                                                  .textColor = THEME_TEXT_MUTED,
                                                  .wrapMode = CLAY_TEXT_WRAP_NONE,
                                              }));
                                }
                                if (line->val[0]) {
                                    Clay_String vs = ui_utils_clay_string(line->val);
                                    CLAY_TEXT(vs,
                                              CLAY_TEXT_CONFIG({
                                                  .fontSize = 12,
                                                  .fontId = FONT_MONO,
                                                  .textColor = line->val_color,
                                                  .wrapMode = CLAY_TEXT_WRAP_NONE,
                                              }));
                                }
                                if (line->trail[0]) {
                                    Clay_String trs = ui_utils_clay_string(line->trail);
                                    CLAY_TEXT(trs,
                                              CLAY_TEXT_CONFIG({
                                                  .fontSize = 12,
                                                  .fontId = FONT_MONO,
                                                  .textColor = THEME_TEXT_MUTED,
                                                  .wrapMode = CLAY_TEXT_WRAP_NONE,
                                              }));
                                }
                            }
                        }
                        if (s_pp_line_count >= JSON_PP_MAX_LINES) {
                            CLAY_TEXT(CLAY_STRING("(truncated at 2048 lines)"), THEME_TEXT_SMALL);
                        }
                    }
                }
            }
        }
    }

    // Click detection for history rows
    for (int ri = 0; ri < match_count; ri++) {
        const MessageRecord* r = message_buf_get(&state->global_history, matches[ri]);
        if (!r) continue;

        Clay_ElementId hc_eid = CLAY_IDI("HC", (uint32_t)ri);
        Clay_ElementId hh_eid = CLAY_IDI("HH", (uint32_t)ri);

        // Copy button - copies full preview to clipboard
        if (Clay_PointerOver(hc_eid) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (r->preview[0] != '\0') {
                SetClipboardText(r->preview);
                s_hist_copied_ts = r->timestamp_us;
                ui_utils_flash_start(&s_hist_copied_timer);
            }
        }
        // Header click (not on Copy) - toggle expand
        if (Clay_PointerOver(hh_eid) && !Clay_PointerOver(hc_eid) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            s_hist_expanded_ts = (s_hist_expanded_ts == r->timestamp_us) ? 0 : r->timestamp_us;
        }
    }
}

static void render_frozen_inspector(AppState* state) {
    bool close_requested = false;

    if (!ui_utils_flash_tick(&s_copied_timer)) s_copied_btn = -1;

    static char meta[128];
    char ts_buf[16];
    util_fmt_hhmmss(state->frozen_message.timestamp_us, ts_buf, sizeof(ts_buf));
    snprintf(meta, sizeof(meta), "%s \xc2\xb7 QoS %u%s", ts_buf, (unsigned)state->frozen_message.qos,
             state->frozen_message.retained ? " \xc2\xb7 Retained" : "");

    CLAY(CLAY_ID("Inspector"),
         {
             .layout =
                 {
                     .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                 },
             .backgroundColor = THEME_BG_PANEL,
             .border = {.width = {.left = 1}, .color = THEME_BORDER},
         }) {
        CLAY(CLAY_ID("InspectorHeader"),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .padding = {14, 14, 10, 10},
                         .childGap = 8,
                         .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                     },
                 .border = {.width = {.bottom = 1}, .color = THEME_BORDER},
             }) {
            CLAY(CLAY_ID("InspectorTopicPath"),
                 {
                     .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}},
                     .clip = {.horizontal = true},
                 }) {
                Clay_String path_str = ui_utils_clay_string(state->frozen_message.topic);
                CLAY_TEXT(path_str,
                          CLAY_TEXT_CONFIG({
                              .fontSize = 16,
                              .fontId = FONT_DEFAULT,
                              .textColor = THEME_TEXT_PRIMARY,
                              .wrapMode = CLAY_TEXT_WRAP_NONE,
                          }));
            }
            CLAY_TEXT(CLAY_STRING("\xe2\x9d\x84 Frozen"),
                      CLAY_TEXT_CONFIG({
                          .fontSize = 11,
                          .fontId = FONT_DEFAULT,
                          .textColor = THEME_ACCENT_BLUE,
                      }));
            CLAY(CLAY_ID("InspectorCloseBtn"),
                 {
                     .layout = {.padding = {8, 8, 4, 4}},
                     .backgroundColor = THEME_BG_BUTTON,
                     .cornerRadius = CLAY_CORNER_RADIUS(3),
                 }) {
                CLAY_TEXT(CLAY_STRING("\xc3\x97"),
                          CLAY_TEXT_CONFIG({
                              .fontSize = 12,
                              .fontId = FONT_DEFAULT,
                              .textColor = THEME_TEXT_MUTED,
                          }));
            }
        }

        CLAY(CLAY_ID("LatestValue"),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .padding = {14, 14, 12, 12},
                     },
                 .backgroundColor = THEME_BG_LATEST,
                 .border = {.width = {.bottom = 1}, .color = THEME_BORDER},
             }) {
            Clay_String meta_str = ui_utils_clay_string(meta);
            CLAY_TEXT(meta_str,
                      CLAY_TEXT_CONFIG({
                          .fontSize = 11,
                          .fontId = FONT_DEFAULT,
                          .textColor = THEME_ACCENT_BLUE,
                      }));
        }

        // History doesn't apply to a single frozen message
        CLAY(CLAY_ID("ViewTabs"),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .childGap = 0,
                     },
                 .border = {.width = {.bottom = 1}, .color = THEME_BORDER},
             }) {
            const char* tab_names[] = {"JSON", "Text", "Hex"};
            ViewMode modes[] = {VIEW_JSON, VIEW_TEXT, VIEW_HEX};
            for (int t = 0; t < 3; t++) {
                bool active = (state->inspector_view == (int)modes[t]);
                CLAY(CLAY_IDI("FTab", (uint32_t)t), {
                    .layout = {.padding = {14, 14, 8, 8}},
                    .backgroundColor = active ? THEME_BG_BUTTON : (Clay_Color){0},
                    .border = active ? (Clay_BorderElementConfig){
                        .width = {.bottom = 2}, .color = THEME_ACCENT_BLUE,
                    }
                                     : (Clay_BorderElementConfig){0},
                }) {
                    Clay_String ts = ui_utils_clay_string(tab_names[t]);
                    CLAY_TEXT(ts,
                              CLAY_TEXT_CONFIG({
                                  .fontSize = 12,
                                  .fontId = FONT_DEFAULT,
                                  .textColor = active ? THEME_TEXT_PRIMARY : THEME_TEXT_MUTED,
                              }));
                }
                if (Clay_PointerOver(CLAY_IDI("FTab", (uint32_t)t)) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    state->inspector_view = modes[t];
                }
            }
            // fallback to json rather than rendering a blank/mismatched view
            if (state->inspector_view == VIEW_HISTORY) state->inspector_view = VIEW_JSON;
        }

        CLAY(CLAY_ID("PayloadView"),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                         .padding = {14, 14, 12, 12},
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,
                         .childGap = 2,
                     },
                 .clip = {.horizontal = true, .vertical = true, .childOffset = Clay_GetScrollOffset()},
             }) {
            switch (state->inspector_view) {
                case VIEW_JSON:
                    render_json_view(state->frozen_message.preview);
                    break;
                case VIEW_TEXT:
                    render_text_view(state->frozen_message.preview);
                    break;
                case VIEW_HEX:
                    render_hex_view(state->frozen_message.preview);
                    break;
                case VIEW_HISTORY:
                    break; // unreachable - coerced to VIEW_JSON above
            }
        }

        CLAY(CLAY_ID("InspectorActions"),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .padding = {14, 14, 8, 8},
                         .childGap = 8,
                     },
                 .border = {.width = {.top = 1}, .color = THEME_BORDER},
             }) {
            const char* actions[] = {"Copy Payload", "Copy Topic", "Publish Here"};
            const char* actions_done[] = {"Copied!", "Copied!", "Publish Here"};
            for (int a = 0; a < 3; a++) {
                bool flashing = (s_copied_btn == a);
                CLAY(CLAY_IDI("FAction", (uint32_t)a), {
                    .layout = {.padding = {10, 10, 4, 4}},
                    .backgroundColor = flashing ? THEME_BG_HOVER : THEME_BG_BUTTON,
                    .cornerRadius = CLAY_CORNER_RADIUS(4),
                    .border = flashing ? (Clay_BorderElementConfig){
                        .width = CLAY_BORDER_OUTSIDE(1), .color = THEME_ACCENT_BLUE,
                    }
                                       : (Clay_BorderElementConfig){0},
                }) {
                    const char* label = flashing ? actions_done[a] : actions[a];
                    Clay_String as = ui_utils_clay_string(label);
                    CLAY_TEXT(as,
                              CLAY_TEXT_CONFIG({
                                  .fontSize = 12,
                                  .fontId = FONT_DEFAULT,
                                  .textColor = flashing ? THEME_ACCENT_BLUE : THEME_TEXT_MUTED,
                              }));
                }
            }
        }
    }

    if (IsKeyPressed(KEY_ESCAPE) && !state->profile_dialog_open && !state->publish_panel_open &&
        !state->context_menu_open) {
        close_requested = true;
    }
    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("InspectorCloseBtn"))) &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        close_requested = true;
    }
    if (close_requested) {
        state->inspector_frozen = false;
    }

    for (int a = 0; a < 3; a++) {
        if (Clay_PointerOver(CLAY_IDI("FAction", (uint32_t)a)) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            switch (a) {
                case 0: // Copy Payload
                    if (state->inspector_view == VIEW_HEX) {
                        static char hex_copy_buf[5248];
                        build_hex_dump_str(state->frozen_message.preview, (int)strlen(state->frozen_message.preview),
                                           hex_copy_buf, sizeof(hex_copy_buf));
                        SetClipboardText(hex_copy_buf);
                    } else {
                        SetClipboardText(state->frozen_message.preview);
                    }
                    break;
                case 1: // Copy Topic
                    SetClipboardText(state->frozen_message.topic);
                    break;
                case 2: // Publish Here
                    util_str_copy(state->publish_topic, sizeof(state->publish_topic), state->frozen_message.topic);
                    state->publish_panel_open = true;
                    break;
                default:
                    break;
            }
            s_copied_btn = a;
            ui_utils_flash_start(&s_copied_timer);
        }
    }
}

void inspector_widget_render(AppState* state) {
    if (state->inspector_frozen) {
        render_frozen_inspector(state);
        return;
    }

    TopicNode* node = state->selected_topic;
    if (!node) return;

    static char full_path[512];
    topic_node_full_path(node, full_path, sizeof(full_path));
    size_t tplen = strlen(full_path);
    if (tplen >= sizeof(s_inspector_topic)) tplen = sizeof(s_inspector_topic) - 1;
    memcpy(s_inspector_topic, full_path, tplen);
    s_inspector_topic[tplen] = '\0';

    static char meta[256];
    if (node->throughput >= 0.05f) {
        snprintf(meta, sizeof(meta), "%u messages \xc2\xb7 %.1f/s%s", node->message_count, node->throughput,
                 node->has_retained ? " \xc2\xb7 Retained" : "");
    } else {
        snprintf(meta, sizeof(meta), "%u messages%s", node->message_count,
                 node->has_retained ? " \xc2\xb7 Retained" : "");
    }

    bool close_requested = false;

    if (!ui_utils_flash_tick(&s_copied_timer)) s_copied_btn = -1;

    CLAY(CLAY_ID("Inspector"),
         {
             .layout =
                 {
                     .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                 },
             .backgroundColor = THEME_BG_PANEL,
             .border = {.width = {.left = 1}, .color = THEME_BORDER},
         }) {
        // hdr: topic path + close button
        CLAY(CLAY_ID("InspectorHeader"),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .padding = {14, 14, 10, 10},
                         .childGap = 8,
                         .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                     },
                 .border = {.width = {.bottom = 1}, .color = THEME_BORDER},
             }) {
            // GROW+clip container so the x is always at the right edge
            CLAY(CLAY_ID("InspectorTopicPath"),
                 {
                     .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}},
                     .clip = {.horizontal = true},
                 }) {
                Clay_String path_str = ui_utils_clay_string(full_path);
                CLAY_TEXT(path_str,
                          CLAY_TEXT_CONFIG({
                              .fontSize = 16,
                              .fontId = FONT_DEFAULT,
                              .textColor = THEME_TEXT_PRIMARY,
                              .wrapMode = CLAY_TEXT_WRAP_NONE,
                          }));
            }
            // Export history of this topic subtree to CSV
            CLAY(CLAY_ID("InspectorExportBtn"),
                 {
                     .layout = {.padding = {8, 8, 4, 4}},
                     .backgroundColor = THEME_BG_BUTTON,
                     .cornerRadius = CLAY_CORNER_RADIUS(3),
                 }) {
                CLAY_TEXT(CLAY_STRING("CSV"),
                          CLAY_TEXT_CONFIG({
                              .fontSize = 12,
                              .fontId = FONT_DEFAULT,
                              .textColor = THEME_TEXT_MUTED,
                          }));
            }
            // Diff toggle
            CLAY(CLAY_ID("InspectorDiffToggle"),
                 {
                     .layout = {.padding = {8, 8, 4, 4}},
                     .backgroundColor = state->diff_enabled ? THEME_BG_HOVER : THEME_BG_BUTTON,
                     .cornerRadius = CLAY_CORNER_RADIUS(3),
                     .border = state->diff_enabled
                         ? (Clay_BorderElementConfig){.width = CLAY_BORDER_OUTSIDE(1), .color = THEME_ACCENT_BLUE}
                         : (Clay_BorderElementConfig){0},
                 }) {
                CLAY_TEXT(CLAY_STRING("Diff"),
                          CLAY_TEXT_CONFIG({
                              .fontSize = 12,
                              .fontId = FONT_DEFAULT,
                              .textColor = state->diff_enabled ? THEME_ACCENT_BLUE : THEME_TEXT_MUTED,
                          }));
            }
            CLAY(CLAY_ID("InspectorCloseBtn"),
                 {
                     .layout = {.padding = {8, 8, 4, 4}},
                     .backgroundColor = THEME_BG_BUTTON,
                     .cornerRadius = CLAY_CORNER_RADIUS(3),
                 }) {
                CLAY_TEXT(CLAY_STRING("\xc3\x97"),
                          CLAY_TEXT_CONFIG({
                              .fontSize = 12,
                              .fontId = FONT_DEFAULT,
                              .textColor = THEME_TEXT_MUTED,
                          }));
            }
        }

        // latest value section
        {
            static char s_age_str[20];
            static char s_last_meta[80];
            if (node->last_message_ts > 0) {
                uint64_t now = util_now_us();
                uint32_t age_s =
                    (now >= node->last_message_ts) ? (uint32_t)((now - node->last_message_ts) / 1000000ULL) : 0;
                if (age_s < 60)
                    snprintf(s_age_str, sizeof(s_age_str), "%us ago", age_s);
                else if (age_s < 3600)
                    snprintf(s_age_str, sizeof(s_age_str), "%um ago", age_s / 60);
                else
                    snprintf(s_age_str, sizeof(s_age_str), "%uh ago", age_s / 3600);
            } else {
                snprintf(s_age_str, sizeof(s_age_str), "--");
            }
            snprintf(s_last_meta, sizeof(s_last_meta), "%s \xc2\xb7 %u B \xc2\xb7 QoS %u", s_age_str,
                     node->last_payload_len, (unsigned)node->last_qos);

            CLAY(CLAY_ID("LatestValue"),
                 {
                     .layout =
                         {
                             .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                             .padding = {14, 14, 12, 12},
                             .layoutDirection = CLAY_TOP_TO_BOTTOM,
                             .childGap = 4,
                         },
                     .backgroundColor = THEME_BG_LATEST,
                     .border = {.width = {.bottom = 1}, .color = THEME_BORDER},
                 }) {
                CLAY_TEXT(CLAY_STRING("Latest Value"), THEME_TEXT_SMALL);

                if (node->last_payload_preview[0] != '\0') {
                    // Show first 150 chars only - full payload is in the tabs below
                    static char s_latest_excerpt[160];
                    const char* full_pv = node->last_payload_preview;
                    size_t full_pv_len = strlen(full_pv);
                    if (full_pv_len > 150) {
                        memcpy(s_latest_excerpt, full_pv, 150);
                        s_latest_excerpt[150] = '.';
                        s_latest_excerpt[151] = '.';
                        s_latest_excerpt[152] = '.';
                        s_latest_excerpt[153] = '\0';
                    } else {
                        memcpy(s_latest_excerpt, full_pv, full_pv_len + 1);
                    }
                    Clay_String val_str = ui_utils_clay_string(s_latest_excerpt);
                    CLAY_TEXT(val_str,
                              CLAY_TEXT_CONFIG({
                                  .fontSize = 13,
                                  .fontId = FONT_MONO,
                                  .textColor = THEME_TEXT_PRIMARY,
                              }));
                } else {
                    CLAY_TEXT(CLAY_STRING("(empty)"), THEME_TEXT_SMALL);
                }

                Clay_String lm_str = ui_utils_clay_string(s_last_meta);
                CLAY_TEXT(lm_str,
                          CLAY_TEXT_CONFIG({
                              .fontSize = 11,
                              .fontId = FONT_DEFAULT,
                              .textColor = THEME_ACCENT_BLUE,
                          }));
                Clay_String meta_str = ui_utils_clay_string(meta);
                CLAY_TEXT(meta_str,
                          CLAY_TEXT_CONFIG({
                              .fontSize = 11,
                              .fontId = FONT_DEFAULT,
                              .textColor = THEME_TEXT_DIM,
                          }));
            }
        }

        // View mode tabs
        CLAY(CLAY_ID("ViewTabs"),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .childGap = 0,
                     },
                 .border = {.width = {.bottom = 1}, .color = THEME_BORDER},
             }) {
            const char* tab_names[] = {"JSON", "Text", "Hex", "History"};
            ViewMode modes[] = {VIEW_JSON, VIEW_TEXT, VIEW_HEX, VIEW_HISTORY};
            for (int t = 0; t < 4; t++) {
                bool active = (state->inspector_view == (int)modes[t]);
                CLAY(CLAY_IDI("Tab", (uint32_t)t), {
                    .layout = {.padding = {14, 14, 8, 8}},
                    .backgroundColor = active ? THEME_BG_BUTTON : (Clay_Color){0},
                    .border = active ? (Clay_BorderElementConfig){
                        .width = {.bottom = 2}, .color = THEME_ACCENT_BLUE,
                    }
                                     : (Clay_BorderElementConfig){0},
                }) {
                    Clay_String ts = ui_utils_clay_string(tab_names[t]);
                    CLAY_TEXT(ts,
                              CLAY_TEXT_CONFIG({
                                  .fontSize = 12,
                                  .fontId = FONT_DEFAULT,
                                  .textColor = active ? THEME_TEXT_PRIMARY : THEME_TEXT_MUTED,
                              }));
                }
                Clay_ElementId tab_eid = CLAY_IDI("Tab", (uint32_t)t);
                if (Clay_PointerOver(tab_eid) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    state->inspector_view = modes[t];
                }
            }
        }

        // Payload view area (scrollable)
        CLAY(CLAY_ID("PayloadView"),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                         .padding = {14, 14, 12, 12},
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,
                         .childGap = 2,
                     },
                 .clip = {.horizontal = true, .vertical = true, .childOffset = Clay_GetScrollOffset()},
             }) {
            switch (state->inspector_view) {
                case VIEW_JSON:
                    if (state->diff_enabled) {
                        render_json_diff_view(node);
                    } else {
                        render_json_view(node->last_payload_preview);
                    }
                    break;
                case VIEW_TEXT:
                    render_text_view(node->last_payload_preview);
                    break;
                case VIEW_HEX:
                    render_hex_view(node->last_payload_preview);
                    break;
                case VIEW_HISTORY:
                    render_history_view(state, node);
                    break;
            }
        }

        // Action buttons
        CLAY(CLAY_ID("InspectorActions"),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .padding = {14, 14, 8, 8},
                         .childGap = 8,
                     },
                 .border = {.width = {.top = 1}, .color = THEME_BORDER},
             }) {
            const char* actions[] = {"Copy Payload", "Copy Topic", "Publish Here", "Clear History"};
            const char* actions_done[] = {"Copied!", "Copied!", "Publish Here", "Cleared!"};
            for (int a = 0; a < 4; a++) {
                bool flashing = (s_copied_btn == a);
                CLAY(CLAY_IDI("Action", (uint32_t)a), {
                    .layout = {.padding = {10, 10, 4, 4}},
                    .backgroundColor = flashing ? THEME_BG_HOVER : THEME_BG_BUTTON,
                    .cornerRadius = CLAY_CORNER_RADIUS(4),
                    .border = flashing ? (Clay_BorderElementConfig){
                        .width = CLAY_BORDER_OUTSIDE(1), .color = THEME_ACCENT_BLUE,
                    }
                                       : (Clay_BorderElementConfig){0},
                }) {
                    const char* label = flashing ? actions_done[a] : actions[a];
                    Clay_String as = ui_utils_clay_string(label);
                    CLAY_TEXT(as,
                              CLAY_TEXT_CONFIG({
                                  .fontSize = 12,
                                  .fontId = FONT_DEFAULT,
                                  .textColor = flashing ? THEME_ACCENT_BLUE : THEME_TEXT_MUTED,
                              }));
                }
            }
        }
    }

    // Close on Escape only when no other panel is consuming keyboard input
    if (IsKeyPressed(KEY_ESCAPE) && !state->profile_dialog_open && !state->publish_panel_open &&
        !state->context_menu_open) {
        close_requested = true;
    }

    if (close_requested) {
        state->selected_topic = NULL;
    }

    // Action button click detection
    for (int a = 0; a < 4; a++) {
        if (Clay_PointerOver(CLAY_IDI("Action", (uint32_t)a)) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            switch (a) {
                case 0: // Copy Payload
                    if (state->inspector_view == VIEW_HEX) {
                        static char hex_copy_buf[5248]; // 64 lines x 82 chars
                        const char* src = node->last_payload_preview;
                        build_hex_dump_str(src, (int)strlen(src), hex_copy_buf, sizeof(hex_copy_buf));
                        SetClipboardText(hex_copy_buf);
                    } else {
                        SetClipboardText(node->last_payload_preview);
                    }
                    break;
                case 1: // Copy Topic
                    SetClipboardText(full_path);
                    break;
                case 2: // Publish Here
                    util_str_copy(state->publish_topic, sizeof(state->publish_topic), full_path);
                    state->publish_panel_open = true;
                    break;
                case 3: // Clear History - reset node display state
                    node->message_count = 0;
                    node->msg_count_str[0] = '\0';
                    node->last_payload_preview[0] = '\0';
                    node->has_retained = false;
                    break;
                default:
                    break;
            }
            s_copied_btn = a;
            ui_utils_flash_start(&s_copied_timer);
        }
    }
}
