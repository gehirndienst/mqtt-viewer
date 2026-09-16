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

#include "model/cbor_pp.h"
#include "model/json_pp.h"
#include "model/message_buf.h"
#include "model/util.h"
#include "ui/inspector_widget.h"
#include "ui/theme.h"
#include "ui/ui_util.h"

#define DIFF_MAX_LINES 256
#define DIFF_LINE_LEN 192
#define HISTORY_MAX_ROWS 200
#define JSON_INDENT_STEP 16
#define PP_GUTTER_W 18
#define TEXT_VIEW_MAX 4000
#define HEX_LINE_BUF 80

typedef enum {
    DIFF_UNCHANGED,
    DIFF_ADDED,
    DIFF_REMOVED,
} DiffState;

typedef struct {
    int curr_idx; // index into s_pp.lines (-1 for REMOVED-only entries)
    int prev_idx; // index into s_diff_prev_text (-1 unless REMOVED or UNCHANGED)
    DiffState state;
} DiffMergedEntry;

// pretty-printers
static JsonPP s_pp;
static JsonPP s_pp_prev;
static JsonPP s_cbor_pp;

// action button flash
static int s_copied_btn = -1;
static float s_copied_timer = 0.0f;

// Strategy: line-wise LCS over the JSON pp output. The diff baseline is the previous payload preview;
// when a new message arrives reprint the previous preview build line-text reps for both previous and current run an LCS
// to mark each current line as UNCHANGED/ADDED and emit synthetic REMOVED rows for prev-only lines
static uint8_t s_diff_prev_payload[TOPIC_PAYLOAD_CAP];
static uint32_t s_diff_prev_len = 0;
static const TopicNode* s_diff_prev_node = NULL;
static uint64_t s_diff_prev_ts = 0;
static char s_diff_prev_text[DIFF_MAX_LINES][DIFF_LINE_LEN];
static int s_diff_prev_text_depth[DIFF_MAX_LINES];
static int s_diff_prev_text_count = 0;
static char s_diff_curr_text[DIFF_MAX_LINES][DIFF_LINE_LEN];
static DiffMergedEntry s_diff_merged[DIFF_MAX_LINES * 2];
static int s_diff_merged_count = 0;
static int s_diff_dp[DIFF_MAX_LINES + 1][DIFF_MAX_LINES + 1];

// history view
static char s_hist_time_bufs[HISTORY_MAX_ROWS][16];
static char s_hist_meta_bufs[HISTORY_MAX_ROWS][32];
// by timestamp (not index) so new messages don't shift state
static uint64_t s_hist_copied_ts = 0;
static float s_hist_copied_timer = 0.0f;
static uint64_t s_hist_expanded_ts = 0;
static TopicNode* s_last_hist_node = NULL;

// Two sanitized copies: Clay copies a Clay_String's chars at draw time, after layout, so a clipboard
// handler running post-layout must not write the buffer the laid-out text still points at
static char s_text_scratch[TOPIC_PAYLOAD_CAP + 1]; // layout only
static char s_clip_scratch[TOPIC_PAYLOAD_CAP + 1]; // clipboard only, never handed to Clay

static char s_inspector_topic[CHART_TOPIC_LEN]; // updated each frame from selected_topic

static const char* payload_text_into(char* dst, size_t dst_cap, const uint8_t* src, uint32_t len, uint32_t cap) {
    if ((size_t)cap + 1 > dst_cap) cap = (uint32_t)(dst_cap - 1);
    if (len > cap) len = cap;
    util_preview_sanitize(dst, (size_t)cap + 1, src, len);
    return dst;
}

static const char* payload_text(const uint8_t* src, uint32_t len, uint32_t cap) {
    return payload_text_into(s_text_scratch, sizeof(s_text_scratch), src, len, cap);
}

// Format a single 16-byte hex-dump line ("offset  hex bytes  |ascii|") into buf without a trailing newline
static int format_hex_line(char* buf, int buf_size, const uint8_t* src, uint32_t len, uint32_t offset) {
    uint32_t n = len - offset;
    if (n > 16) n = 16;
    int pos = 0;
    pos += snprintf(buf + pos, buf_size - pos, "%08x  ", offset);
    for (uint32_t j = 0; j < 16; j++) {
        if (j < n)
            pos += snprintf(buf + pos, buf_size - pos, "%02x ", src[offset + j]);
        else
            pos += snprintf(buf + pos, buf_size - pos, "   ");
        if (j == 7) pos += snprintf(buf + pos, buf_size - pos, " ");
    }
    pos += snprintf(buf + pos, buf_size - pos, " |");
    for (uint32_t j = 0; j < n && pos < buf_size - 2; j++) {
        uint8_t c = src[offset + j];
        buf[pos++] = (c >= 0x20 && c < 0x7f) ? (char)c : '.';
    }
    buf[pos++] = '|';
    buf[pos] = '\0';
    return pos;
}

static void build_hex_dump_str(const uint8_t* src, uint32_t len, char* out, int out_size) {
    int pos = 0;
    char line[HEX_LINE_BUF];
    for (uint32_t offset = 0; offset < len; offset += 16) {
        int n = format_hex_line(line, sizeof(line), src, len, offset);
        if (pos + n + 1 >= out_size) break; // leave room for '\n' and terminator
        memcpy(out + pos, line, (size_t)n);
        pos += n;
        out[pos++] = '\n';
    }
    if (pos > 0 && out[pos - 1] == '\n') pos--;
    out[pos] = '\0';
}

// Start charting the numeric value on pretty-printed line
static void chart_add_from_line(AppState* state, int line_idx) {
    if (line_idx < 0 || line_idx >= s_pp.line_count) return;
    if (s_inspector_topic[0] == '\0') return;
    const JsonPPLine* line = &s_pp.lines[line_idx];
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

static Clay_Color pp_val_color(JsonPPValKind kind) {
    switch (kind) {
        case JSON_PP_VAL_STRING:
            return THEME_GREEN;
        case JSON_PP_VAL_ATOM:
            return THEME_PINK;
        case JSON_PP_VAL_PUNCT:
        default:
            return THEME_TEXT_MUTED;
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

static void render_pp_line(const JsonPPLine* line, int line_idx, DiffState st, bool chartable) {
    uint16_t left_pad = (uint16_t)(line->depth * JSON_INDENT_STEP);

    Clay_Color key_c = (st == DIFF_ADDED) ? THEME_DIFF_ADDED : THEME_LIGHT_BLUE;
    Clay_Color sep_c = (st == DIFF_ADDED) ? THEME_DIFF_ADDED : THEME_TEXT_MUTED;
    Clay_Color val_c = (st == DIFF_ADDED) ? THEME_DIFF_ADDED : pp_val_color(line->val_kind);
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
            if (chartable && line->is_numeric) {
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

static void render_text_view(const uint8_t* src, uint32_t len) {
    if (len == 0) {
        CLAY_TEXT(CLAY_STRING("(no payload)"), THEME_TEXT_SMALL);
        return;
    }
    Clay_String ts = ui_utils_clay_string(payload_text(src, len, TEXT_VIEW_MAX));
    CLAY_TEXT(ts, THEME_TEXT_MONO);
    if (len > TEXT_VIEW_MAX) {
        CLAY_TEXT(CLAY_STRING("... (use JSON tab for large payloads)"), THEME_TEXT_SMALL);
    }
}

static void render_hex_view(const uint8_t* src, uint32_t len) {
    if (len == 0) {
        CLAY_TEXT(CLAY_STRING("(no payload)"), THEME_TEXT_SMALL);
        return;
    }

    // cap at 64 lines (1024 bytes shown)
    static char hex_lines[64][HEX_LINE_BUF];
    int line_count = 0;
    for (uint32_t offset = 0; offset < len && line_count < 64; offset += 16) {
        format_hex_line(hex_lines[line_count], HEX_LINE_BUF, src, len, offset);
        line_count++;
    }

    for (int i = 0; i < line_count; i++) {
        Clay_String hs = ui_utils_clay_string(hex_lines[i]);
        CLAY_TEXT(hs, THEME_TEXT_MONO);
    }
    if (len > 1024) {
        CLAY_TEXT(CLAY_STRING("... (first 1024 bytes shown)"), THEME_TEXT_SMALL);
    }
}

static void render_json_view(const uint8_t* src, uint32_t len, uint64_t key) {
    if (len == 0) {
        CLAY_TEXT(CLAY_STRING("(no payload)"), THEME_TEXT_SMALL);
        return;
    }
    if (!json_pp_looks_like_json((const char*)src, len)) {
        render_text_view(src, len);
        return;
    }

    json_pp_run_cached(&s_pp, (const char*)src, len, key);

    if (s_pp.line_count == 0) {
        render_text_view(src, len);
        return;
    }

    for (int li = 0; li < s_pp.line_count; li++) {
        render_pp_line(&s_pp.lines[li], li, DIFF_UNCHANGED, true);
    }
    if (s_pp.line_count >= JSON_PP_MAX_LINES) {
        CLAY_TEXT(CLAY_STRING("(output truncated at 2048 lines)"), THEME_TEXT_SMALL);
    }
}

static void render_json_diff_view(TopicNode* node) {
    uint32_t len = 0;
    const uint8_t* src = topic_node_payload(node, &len);
    if (len == 0) {
        CLAY_TEXT(CLAY_STRING("(no payload)"), THEME_TEXT_SMALL);
        return;
    }
    if (!json_pp_looks_like_json((const char*)src, len)) {
        // Diff is JSON-only by design - fallback to plain text view for non-JSON payloads
        render_text_view(src, len);
        return;
    }

    json_pp_run_cached(&s_pp, (const char*)src, len, node->last_display_update_us);
    if (s_pp.line_count == 0) {
        render_text_view(src, len);
        return;
    }

    bool node_switched = (s_diff_prev_node != node);
    bool ts_advanced = (s_diff_prev_ts != node->last_message_ts);

    if (node_switched) {
        // First frame on this node - establish baseline, render plain
        s_diff_prev_node = node;
        s_diff_prev_ts = node->last_message_ts;
        memcpy(s_diff_prev_payload, src, len);
        s_diff_prev_len = len;
        s_diff_merged_count = 0;
        s_diff_prev_text_count = 0;
    } else if (ts_advanced) {
        int curr_n = s_pp.line_count > DIFF_MAX_LINES ? DIFF_MAX_LINES : s_pp.line_count;
        static int curr_depth[DIFF_MAX_LINES];
        for (int i = 0; i < curr_n; i++) {
            diff_format_line(&s_pp.lines[i], s_diff_curr_text[i]);
            curr_depth[i] = s_pp.lines[i].depth;
        }

        // Format the previous payload into its own instance so the current lines stay put
        json_pp_run_len(&s_pp_prev, (const char*)s_diff_prev_payload, s_diff_prev_len);
        int prev_n = s_pp_prev.line_count > DIFF_MAX_LINES ? DIFF_MAX_LINES : s_pp_prev.line_count;
        for (int i = 0; i < prev_n; i++) {
            diff_format_line(&s_pp_prev.lines[i], s_diff_prev_text[i]);
            s_diff_prev_text_depth[i] = s_pp_prev.lines[i].depth;
        }
        s_diff_prev_text_count = prev_n;

        diff_compute(prev_n, curr_n, curr_depth);

        // Update baseline to the current payload
        memcpy(s_diff_prev_payload, src, len);
        s_diff_prev_len = len;
        s_diff_prev_ts = node->last_message_ts;
    }
    // else: cached merged sequence from prior frame remains valid

    // rndr
    if (s_diff_merged_count == 0) {
        for (int li = 0; li < s_pp.line_count; li++) {
            render_pp_line(&s_pp.lines[li], li, DIFF_UNCHANGED, true);
        }
    } else {
        for (int k = 0; k < s_diff_merged_count; k++) {
            DiffMergedEntry* e = &s_diff_merged[k];
            if (e->state == DIFF_REMOVED) {
                render_pp_removed(k, e->prev_idx);
            } else {
                render_pp_line(&s_pp.lines[e->curr_idx], e->curr_idx, e->state, true);
            }
        }
        for (int li = DIFF_MAX_LINES; li < s_pp.line_count; li++) {
            render_pp_line(&s_pp.lines[li], li, DIFF_UNCHANGED, true);
        }
    }
    if (s_pp.line_count >= JSON_PP_MAX_LINES) {
        CLAY_TEXT(CLAY_STRING("(output truncated at 2048 lines)"), THEME_TEXT_SMALL);
    }
}

// Chart capture is JSON-only, so CBOR lines get no [+] - a series on a CBOR field would never fill
static void render_cbor_view(const uint8_t* src, uint32_t len, uint64_t key) {
    if (len == 0) {
        CLAY_TEXT(CLAY_STRING("(no payload)"), THEME_TEXT_SMALL);
        return;
    }
    if (!cbor_pp_run_cached(&s_cbor_pp, src, len, key)) {
        CLAY_TEXT(CLAY_STRING("(not a well-formed CBOR item - showing as text)"), THEME_TEXT_SMALL);
        render_text_view(src, len);
        return;
    }
    for (int li = 0; li < s_cbor_pp.line_count; li++) {
        render_pp_line(&s_cbor_pp.lines[li], li, DIFF_UNCHANGED, false);
    }
    if (s_cbor_pp.line_count >= JSON_PP_MAX_LINES) {
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
    static int match_count = 0;
    static const TopicNode* s_match_node = NULL;
    static uint64_t s_match_generation = ~0ULL;
    uint64_t generation = message_buf_generation(&state->global_history);
    if (node != s_match_node || generation != s_match_generation) {
        s_match_node = node;
        s_match_generation = generation;
        match_count = 0;
        for (int i = (int)total - 1; i >= 0 && match_count < HISTORY_MAX_ROWS; i--) {
            const MessageRecord* r = message_buf_get(&state->global_history, (uint32_t)i);
            if (r && strcmp(r->topic, hist_path) == 0) matches[match_count++] = (uint32_t)i;
        }
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
            if (expanded && r->payload_len > 0) {
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
                    const uint8_t* src = r->payload;
                    bool is_json = json_pp_looks_like_json((const char*)src, r->payload_len);
                    if (is_json) json_pp_run_cached(&s_pp, (const char*)src, r->payload_len, r->timestamp_us);
                    if (!is_json || s_pp.line_count == 0) {
                        Clay_String raw_cs = ui_utils_clay_string(payload_text(src, r->payload_len, TEXT_VIEW_MAX));
                        CLAY_TEXT(raw_cs,
                                  CLAY_TEXT_CONFIG({
                                      .fontSize = 12,
                                      .fontId = FONT_MONO,
                                      .textColor = THEME_TEXT_SECONDARY,
                                  }));
                    } else {
                        for (int li = 0; li < s_pp.line_count; li++) {
                            JsonPPLine* line = &s_pp.lines[li];
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
                                                  .textColor = THEME_LIGHT_BLUE,
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
                                                  .textColor = pp_val_color(line->val_kind),
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
                        if (s_pp.line_count >= JSON_PP_MAX_LINES) {
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
            if (r->payload_len > 0) {
                SetClipboardText(payload_text_into(s_clip_scratch, sizeof(s_clip_scratch), r->payload, r->payload_len,
                                                   TOPIC_PAYLOAD_CAP));
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

    // Search results carry only the sanitized preview (db_search_messages() leaves payload NULL)
    const MessageRecord* fzm = &state->frozen_message;
    bool fz_raw = (fzm->payload && fzm->payload_len > 0);
    const uint8_t* fz = fz_raw ? fzm->payload : (const uint8_t*)fzm->preview;
    uint32_t fz_len = fz_raw ? fzm->payload_len : (uint32_t)strlen(fzm->preview);

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
            const char* tab_names[] = {"JSON", "CBOR", "Hex"};
            ViewMode modes[] = {VIEW_JSON, VIEW_CBOR, VIEW_HEX};
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
                    render_json_view(fz, fz_len, state->frozen_message.timestamp_us);
                    break;
                case VIEW_CBOR:
                    render_cbor_view(fz, fz_len, state->frozen_message.timestamp_us);
                    break;
                case VIEW_HEX:
                    render_hex_view(fz, fz_len);
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
                        build_hex_dump_str(fz, fz_len, hex_copy_buf, sizeof(hex_copy_buf));
                        SetClipboardText(hex_copy_buf);
                    } else {
                        SetClipboardText(
                            payload_text_into(s_clip_scratch, sizeof(s_clip_scratch), fz, fz_len, TOPIC_PAYLOAD_CAP));
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

    uint32_t pl_len = 0;
    const uint8_t* pl = topic_node_payload(node, &pl_len);

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

                if (pl_len > 0) {
                    // Show the first ~150 chars only - full payload is in the tabs below
                    static char s_latest_excerpt[160];
                    util_preview_build_compact(s_latest_excerpt, sizeof(s_latest_excerpt), pl, pl_len);
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
            const char* tab_names[] = {"JSON", "CBOR", "Hex", "History"};
            ViewMode modes[] = {VIEW_JSON, VIEW_CBOR, VIEW_HEX, VIEW_HISTORY};
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
                        render_json_view(pl, pl_len, node->last_display_update_us);
                    }
                    break;
                case VIEW_CBOR:
                    render_cbor_view(pl, pl_len, node->last_display_update_us);
                    break;
                case VIEW_HEX:
                    render_hex_view(pl, pl_len);
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

    // Chart [+] buttons next to numeric JSON lines - only lines that were laid out this frame can be hit
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        for (int li = 0; li < s_pp.line_count; li++) {
            if (!s_pp.lines[li].is_numeric) continue;
            if (Clay_PointerOver(CLAY_IDI("ChartAdd", (uint32_t)li))) {
                chart_add_from_line(state, li);
                break;
            }
        }
    }

    // Action button click detection
    for (int a = 0; a < 4; a++) {
        if (Clay_PointerOver(CLAY_IDI("Action", (uint32_t)a)) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            switch (a) {
                case 0: // Copy Payload
                    if (state->inspector_view == VIEW_HEX) {
                        static char hex_copy_buf[5248]; // 64 lines x 82 chars
                        build_hex_dump_str(pl, pl_len, hex_copy_buf, sizeof(hex_copy_buf));
                        SetClipboardText(hex_copy_buf);
                    } else {
                        SetClipboardText(
                            payload_text_into(s_clip_scratch, sizeof(s_clip_scratch), pl, pl_len, TOPIC_PAYLOAD_CAP));
                    }
                    break;
                case 1: // Copy Topic
                    SetClipboardText(full_path);
                    break;
                case 2: // Publish Here
                    util_str_copy(state->publish_topic, sizeof(state->publish_topic), full_path);
                    state->publish_panel_open = true;
                    break;
                case 3: // Clear History - main.c owns the history ring and the DB, so hand it over
                    state->clear_topic_requested = node;
                    break;
                default:
                    break;
            }
            s_copied_btn = a;
            ui_utils_flash_start(&s_copied_timer);
        }
    }
}
