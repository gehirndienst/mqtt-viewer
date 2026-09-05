// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include "ui/chart_panel.h"
#include "model/util.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cjson/cJSON.h>

#include "clay.h"
#include "raylib.h"

#include "model/chart_series.h"
#include "platform/ui.h"
#include "ui/theme.h"
#include "ui/ui_util.h"

#define CHART_PANEL_HEIGHT_PX 280.0f
#define CHART_CARD_HEADER_PX 18

static void chart_panel_grid(int n, int* cols, int* rows) {
    if (n <= 1) {
        *cols = 1;
        *rows = 1;
        return;
    }
    if (n <= 4) {
        *cols = 2;
        *rows = (n + 1) / 2;
        return;
    }
    if (n <= 9) {
        *cols = 3;
        *rows = (n + 2) / 3;
        return;
    }
    *cols = 4;
    *rows = (n + 3) / 4;
}

static bool node_to_double(const cJSON* node, double* out) {
    if (!node) return false;
    if (cJSON_IsNumber(node)) {
        if (!isfinite(node->valuedouble)) return false;
        *out = node->valuedouble;
        return true;
    }
    if (cJSON_IsString(node) && node->valuestring) {
        char* end = NULL;
        double v = strtod(node->valuestring, &end);
        if (end == node->valuestring || !isfinite(v)) return false;
        *out = v;
        return true;
    }
    return false;
}

static bool extract_double(const uint8_t* json, uint32_t len, const char* dot_path, double* out) {
    if (!json || len == 0 || !out) return false;

    if (!dot_path || dot_path[0] == '\0') {
        char buf[64];
        uint32_t n = len < sizeof(buf) - 1 ? len : (uint32_t)(sizeof(buf) - 1);
        memcpy(buf, json, n);
        buf[n] = '\0';
        char* end = NULL;
        double v = strtod(buf, &end);
        if (end == buf || !isfinite(v)) return false;
        *out = v;
        return true;
    }

    cJSON* root = cJSON_ParseWithLength((const char*)json, len);
    if (!root) return false;

    const cJSON* node = root;
    const char* p = dot_path;
    char seg[64];
    while (*p && node) {
        const char* dot = strchr(p, '.');
        size_t slen = dot ? (size_t)(dot - p) : strlen(p);
        if (slen >= sizeof(seg)) slen = sizeof(seg) - 1;
        memcpy(seg, p, slen);
        seg[slen] = '\0';
        if (cJSON_IsObject(node)) {
            node = cJSON_GetObjectItemCaseSensitive(node, seg);
        } else if (cJSON_IsArray(node)) {
            char* end = NULL;
            long idx = strtol(seg, &end, 10);
            if (end == seg || idx < 0) {
                node = NULL;
                break;
            }
            node = cJSON_GetArrayItem(node, (int)idx);
        } else {
            node = NULL;
            break;
        }
        p = dot ? dot + 1 : p + slen;
    }

    bool ok = node_to_double(node, out);
    cJSON_Delete(root);
    return ok;
}

static int chart_panel_active_at(const AppState* state, int i) {
    int seen = 0;
    for (int s = 0; s < CHART_MAX_SERIES; s++) {
        if (!state->chart_series[s].active) continue;
        if (seen == i) return s;
        seen++;
    }
    return -1;
}

bool chart_panel_has_active(const AppState* state) {
    for (int i = 0; i < CHART_MAX_SERIES; i++) {
        if (state->chart_series[i].active) return true;
    }
    return false;
}

void chart_panel_render(AppState* state) {
    int active_count = 0;
    for (int i = 0; i < CHART_MAX_SERIES; i++) {
        if (state->chart_series[i].active) active_count++;
    }
    if (active_count == 0) return;

    int cols = 1, rows = 1;
    chart_panel_grid(active_count, &cols, &rows);

    static char val_bufs[CHART_MAX_SERIES][32];

    CLAY(CLAY_ID("ChartsPanel"),
         {
             .layout =
                 {
                     .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(CHART_PANEL_HEIGHT_PX)},
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .padding = {6, 6, 6, 6},
                     .childGap = 6,
                 },
             .backgroundColor = THEME_BG_PANEL,
             .border = {.width = {.top = 1}, .color = THEME_BORDER},
         }) {
        for (int r = 0; r < rows; r++) {
            CLAY(CLAY_IDI("ChartRow", (uint32_t)r),
                 {
                     .layout =
                         {
                             .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                             .childGap = 6,
                         },
                 }) {
                for (int c = 0; c < cols; c++) {
                    int slot = r * cols + c;
                    int si = chart_panel_active_at(state, slot);
                    if (si < 0) {
                        // empty cell: still grow so siblings divide width evenly
                        CLAY(CLAY_IDI("ChartEmpty", (uint32_t)slot),
                             {.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}}) {}
                        continue;
                    }
                    ChartSeries* ser = &state->chart_series[si];
                    if (ser->count > 0) {
                        const ChartSample* last = chart_series_get(ser, ser->count - 1);
                        snprintf(val_bufs[si], sizeof(val_bufs[si]), "%.4g", last->value);
                    } else {
                        snprintf(val_bufs[si], sizeof(val_bufs[si]), "—");
                    }

                    CLAY(CLAY_IDI("ChartCard", (uint32_t)si),
                         {
                             .layout =
                                 {
                                     .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                                     .padding = {4, 4, 4, 4},
                                     .childGap = 2,
                                 },
                             .backgroundColor = THEME_BG_BAR,
                             .border = {.width = CLAY_BORDER_OUTSIDE(1), .color = THEME_BORDER},
                             .cornerRadius = CLAY_CORNER_RADIUS(3),
                         }) {
                        // Header: topic/path + current value + remove x
                        CLAY(CLAY_IDI("ChartHdr", (uint32_t)si),
                             {
                                 .layout =
                                     {
                                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(CHART_CARD_HEADER_PX)},
                                         .childGap = 6,
                                         .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                                     },
                             }) {
                            const char* label = ser->dot_path[0] ? ser->dot_path : "(payload)";
                            Clay_String pl = ui_utils_clay_string(label);
                            CLAY_TEXT(pl,
                                      CLAY_TEXT_CONFIG({.fontSize = 11,
                                                        .fontId = FONT_MONO,
                                                        .textColor = THEME_TEXT_PRIMARY,
                                                        .wrapMode = CLAY_TEXT_WRAP_NONE}));

                            // Per-card spacer ID - must be unique across all rendered cards
                            CLAY(CLAY_IDI("ChartHdrSp", (uint32_t)si),
                                 {.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) {}
                            Clay_String vc = ui_utils_clay_string(val_bufs[si]);
                            CLAY_TEXT(vc,
                                      CLAY_TEXT_CONFIG(
                                          {.fontSize = 11, .fontId = FONT_MONO, .textColor = THEME_ACCENT_BLUE}));
                            CLAY(CLAY_IDI("ChartRm", (uint32_t)si),
                                 {
                                     .layout = {.padding = {5, 5, 0, 0}},
                                     .backgroundColor = THEME_BG_BUTTON,
                                     .cornerRadius = CLAY_CORNER_RADIUS(2),
                                 }) {
                                CLAY_TEXT(CLAY_STRING("\xc3\x97"),
                                          CLAY_TEXT_CONFIG(
                                              {.fontSize = 11, .fontId = FONT_DEFAULT, .textColor = THEME_TEXT_MUTED}));
                            }
                        }
                        // plot reservation: chart_panel_draw() paints into this rect
                        CLAY(CLAY_IDI("Plot", (uint32_t)si),
                             {
                                 .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}},
                                 .backgroundColor = THEME_BG_PLOT,
                             }) {}
                    }
                }
            }
        }
    }
}

void chart_panel_draw(AppState* state) {
    Vector2 mouse = GetMousePosition();
    Font label_font = ui_get_font(FONT_MONO);
    Color grid_col = (Color){80, 80, 100, 90};
    Color line_col = (Color){102, 153, 255, 255};
    Color cursor_col = (Color){200, 200, 220, 220};
    Color label_col = (Color){136, 136, 136, 220};
    Color tooltip_bg = (Color){25, 30, 45, 235};
    Color tooltip_fg = (Color){230, 230, 240, 255};

    for (int csi = 0; csi < CHART_MAX_SERIES; csi++) {
        ChartSeries* ser = &state->chart_series[csi];
        if (!ser->active) continue;
        Clay_ElementData ed = Clay_GetElementData(CLAY_IDI("Plot", (uint32_t)csi));
        if (!ed.found) continue;
        Clay_BoundingBox b = ed.boundingBox;
        if (b.width < 4.0f || b.height < 4.0f) continue;

        if (ser->y_dirty) chart_series_recompute_minmax(ser);
        float y_min = (float)ser->y_min;
        float y_max = (float)ser->y_max;
        float y_range = y_max - y_min;
        if (y_range < 1.0e-9f) {
            y_min -= 0.5f;
            y_max += 0.5f;
            y_range = 1.0f;
        }

        // Reserve a strip at the bottom for the time-axis labels
        const float pad_x = 4.0f;
        const float pad_top = 4.0f;
        const float pad_bot = 14.0f;
        float px0 = b.x + pad_x;
        float py0 = b.y + pad_top;
        float pw = b.width - 2 * pad_x;
        float ph = b.height - pad_top - pad_bot;
        if (pw < 4.0f || ph < 4.0f) continue;

        // Grid: 3 horizontal divisions at quarters of the plot area
        for (int g = 1; g < 4; g++) {
            float gy = py0 + (float)g * ph / 4.0f;
            DrawLineEx((Vector2){px0, gy}, (Vector2){px0 + pw, gy}, 1.0f, grid_col);
        }

        // Line strip + circle markers per sample
        uint32_t n = ser->count;
        uint64_t t_first = 0, t_last = 0;
        double t_range_us = 1.0;
        if (n >= 1) {
            t_first = chart_series_get(ser, 0)->ts_us;
            t_last = chart_series_get(ser, n - 1)->ts_us;
            t_range_us = (double)(t_last - t_first);
            if (t_range_us < 1.0) t_range_us = 1.0;
        }
        if (n >= 2) {
            Vector2 prev_pt = {0};
            for (uint32_t i = 0; i < n; i++) {
                const ChartSample* sm = chart_series_get(ser, i);
                float fx = px0 + (float)((double)(sm->ts_us - t_first) / t_range_us) * pw;
                float fy = py0 + ph - ((float)(sm->value - y_min) / y_range) * ph;
                Vector2 cur_pt = {fx, fy};
                if (i > 0) DrawLineEx(prev_pt, cur_pt, 1.5f, line_col);
                prev_pt = cur_pt;
            }
        }

        // Y-axis labels (min/max at the corners of the plot area)
        char ymin_buf[24], ymax_buf[24];
        snprintf(ymin_buf, sizeof(ymin_buf), "%.4g", ser->y_min);
        snprintf(ymax_buf, sizeof(ymax_buf), "%.4g", ser->y_max);
        DrawTextEx(label_font, ymax_buf, (Vector2){px0 + 2, py0 + 2}, 10.0f, 1.0f, label_col);
        DrawTextEx(label_font, ymin_buf, (Vector2){px0 + 2, py0 + ph - 12}, 10.0f, 1.0f, label_col);

        // X-axis time labels (first sample at left, last at right)
        if (n >= 1) {
            char tfirst_buf[16], tlast_buf[16];
            util_fmt_hhmmss(t_first, tfirst_buf, sizeof(tfirst_buf));
            util_fmt_hhmmss(t_last, tlast_buf, sizeof(tlast_buf));
            DrawTextEx(label_font, tfirst_buf, (Vector2){px0, py0 + ph + 1}, 10.0f, 1.0f, label_col);
            Vector2 tlast_size = MeasureTextEx(label_font, tlast_buf, 10.0f, 1.0f);
            DrawTextEx(label_font, tlast_buf, (Vector2){px0 + pw - tlast_size.x, py0 + ph + 1}, 10.0f, 1.0f, label_col);
        }

        // Cursor hover: find the sample nearest the mouse-x and draw a vertical
        // tracker line plus a tooltip with its value and timestamp
        bool inside = ui_utils_bbox_contains(b, mouse.x, mouse.y);
        if (inside && n >= 1) {
            uint32_t best = 0;
            float best_dx = 1.0e30f;
            float fx_best = px0;
            float fy_best = py0;
            for (uint32_t i = 0; i < n; i++) {
                const ChartSample* sm = chart_series_get(ser, i);
                float fx = px0 + (float)((double)(sm->ts_us - t_first) / t_range_us) * pw;
                float dx = fx - (float)mouse.x;
                if (dx < 0) dx = -dx;
                if (dx < best_dx) {
                    best_dx = dx;
                    best = i;
                    fx_best = fx;
                    fy_best = py0 + ph - ((float)(sm->value - y_min) / y_range) * ph;
                }
            }
            const ChartSample* hit = chart_series_get(ser, best);
            // Vertical tracker
            DrawLineEx((Vector2){fx_best, py0}, (Vector2){fx_best, py0 + ph}, 1.0f, cursor_col);
            DrawCircleV((Vector2){fx_best, fy_best}, 3.0f, line_col);
            DrawCircleLines((int)fx_best, (int)fy_best, 4.0f, cursor_col);

            // Tooltip with timestamp + value, anchored above the tracker
            char ts_buf[16];
            char val_buf[24];
            util_fmt_hhmmss(hit->ts_us, ts_buf, sizeof(ts_buf));
            snprintf(val_buf, sizeof(val_buf), "%.4g", hit->value);
            char tip[48]; // ts_buf(15) + 2 spaces + val_buf(23) + NUL = 41 max
            snprintf(tip, sizeof(tip), "%s  %s", ts_buf, val_buf);
            Vector2 ts = MeasureTextEx(label_font, tip, 11.0f, 1.0f);
            float tx = fx_best + 6.0f;
            float ty = fy_best - ts.y - 6.0f;
            if (tx + ts.x + 6.0f > b.x + b.width) tx = fx_best - ts.x - 10.0f;
            if (ty < b.y) ty = fy_best + 8.0f;
            DrawRectangleRec((Rectangle){tx - 4, ty - 2, ts.x + 8, ts.y + 4}, tooltip_bg);
            DrawTextEx(label_font, tip, (Vector2){tx, ty}, 11.0f, 1.0f, tooltip_fg);
        }
    }
}

void chart_panel_capture_sample(AppState* state, const char* topic, const uint8_t* payload, uint32_t payload_len,
                                uint64_t ts_us) {
    if (!payload || payload_len == 0) return;
    for (int i = 0; i < CHART_MAX_SERIES; i++) {
        ChartSeries* s = &state->chart_series[i];
        if (!s->active) continue;
        if (strcmp(s->topic, topic) != 0) continue;
        double v;
        if (extract_double(payload, payload_len, s->dot_path, &v)) {
            chart_series_push_sample(s, ts_us, v);
        }
    }
}
