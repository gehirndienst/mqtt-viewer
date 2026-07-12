// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "clay.h"
#include "raylib.h"

#include "model/connection_log.h"
#include "ui/log_panel.h"
#include "ui/theme.h"

#define LOG_VISIBLE_ROWS 64

static char s_log_bufs[LOG_VISIBLE_ROWS][320];
static char s_level_bufs[LOG_VISIBLE_ROWS][8];
static char s_msg_bufs[LOG_VISIBLE_ROWS][260];
static char s_row_ids[LOG_VISIBLE_ROWS][32];

void log_panel_render(AppState* state) {
    if (!state->log_panel_open) return;

    uint32_t count = connection_log_count(&state->conn_log);

    bool do_clear = false;
    bool do_close = false;

    CLAY(CLAY_ID("LogPanel"),
         {
             .layout =
                 {
                     .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(200)},
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                 },
             .backgroundColor = THEME_BG_PANEL,
             .border = {.width = {.top = 1}, .color = THEME_BORDER},
         }) {
        CLAY(CLAY_ID("LogHeader"),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .layoutDirection = CLAY_LEFT_TO_RIGHT,
                         .padding = {12, 12, 6, 6},
                         .childGap = 8,
                         .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                     },
                 .backgroundColor = THEME_BG_BAR,
                 .border = {.width = {.bottom = 1}, .color = THEME_BORDER},
             }) {
            CLAY_TEXT(CLAY_STRING("Connection Log"), THEME_TEXT_HEADING);

            CLAY(CLAY_ID("LogHeaderSpacer"),
                 {
                     .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}},
                 }) {}

            CLAY(CLAY_ID("LogClearBtn"),
                 {
                     .layout = {.padding = {10, 10, 4, 4}},
                     .backgroundColor = THEME_BG_BUTTON,
                     .cornerRadius = CLAY_CORNER_RADIUS(3),
                 }) {
                CLAY_TEXT(CLAY_STRING("Clear"), THEME_TEXT_SMALL);
            }

            CLAY(CLAY_ID("LogCloseBtn"),
                 {
                     .layout = {.padding = {8, 8, 4, 4}},
                     .backgroundColor = THEME_BG_BUTTON,
                     .cornerRadius = CLAY_CORNER_RADIUS(3),
                 }) {
                CLAY_TEXT(CLAY_STRING("\xc3\x97"),
                          CLAY_TEXT_CONFIG({
                              .fontSize = 14,
                              .fontId = FONT_DEFAULT,
                              .textColor = THEME_TEXT_MUTED,
                          }));
            }
        }
        CLAY(CLAY_ID("LogScroll"),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,
                         .padding = {4, 4, 4, 4},
                         .childGap = 2,
                     },
             }) {
            uint32_t display_count = count < (uint32_t)LOG_VISIBLE_ROWS ? count : (uint32_t)LOG_VISIBLE_ROWS;

            // Iterate newest-first: from count-1 down to count-display_count
            for (uint32_t di = 0; di < display_count; di++) {
                uint32_t entry_idx = count - 1u - di;

                LogEntry entry;
                if (!connection_log_get(&state->conn_log, entry_idx, &entry)) continue;

                snprintf(s_row_ids[di], sizeof(s_row_ids[di]), "LogRow_%u", di);

                time_t secs = (time_t)(entry.timestamp_us / 1000000u);
                struct tm tm_info;
                localtime_r(&secs, &tm_info);
                strftime(s_log_bufs[di], sizeof(s_log_bufs[di]), "%H:%M:%S", &tm_info);

                const char* level_label;
                Clay_Color level_color;
                switch (entry.level) {
                    case CONN_LOG_WARN:
                        level_label = "WARN";
                        level_color = THEME_ORANGE;
                        break;
                    case CONN_LOG_ERROR:
                        level_label = "ERR ";
                        level_color = THEME_RED;
                        break;
                    case CONN_LOG_INFO:
                    default:
                        level_label = "INFO";
                        level_color = THEME_ACCENT_BLUE;
                        break;
                }

                snprintf(s_level_bufs[di], sizeof(s_level_bufs[di]), "%s", level_label);
                snprintf(s_msg_bufs[di], sizeof(s_msg_bufs[di]), "%s", entry.message);

                Clay_String row_id_cs = {
                    .length = (int32_t)strlen(s_row_ids[di]),
                    .chars = s_row_ids[di],
                };
                Clay_String ts_cs = {
                    .length = (int32_t)strlen(s_log_bufs[di]),
                    .chars = s_log_bufs[di],
                };
                Clay_String level_cs = {
                    .length = (int32_t)strlen(s_level_bufs[di]),
                    .chars = s_level_bufs[di],
                };
                Clay_String msg_cs = {
                    .length = (int32_t)strlen(s_msg_bufs[di]),
                    .chars = s_msg_bufs[di],
                };

                CLAY(CLAY_SID(row_id_cs),
                     {
                         .layout =
                             {
                                 .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                                 .layoutDirection = CLAY_LEFT_TO_RIGHT,
                                 .padding = {8, 8, 3, 3},
                                 .childGap = 8,
                                 .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                             },
                     }) {
                    // Timestamp (dimmed)
                    CLAY_TEXT(ts_cs,
                              CLAY_TEXT_CONFIG({
                                  .fontSize = 12,
                                  .fontId = FONT_MONO,
                                  .textColor = THEME_TEXT_DIM,
                              }));

                    // Level badge
                    CLAY_TEXT(level_cs,
                              CLAY_TEXT_CONFIG({
                                  .fontSize = 12,
                                  .fontId = FONT_MONO,
                                  .textColor = level_color,
                              }));

                    // Message
                    CLAY_TEXT(msg_cs,
                              CLAY_TEXT_CONFIG({
                                  .fontSize = 13,
                                  .fontId = FONT_DEFAULT,
                                  .textColor = THEME_TEXT_SECONDARY,
                              }));
                }
            }
        }
    }

    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("LogClearBtn"))) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        do_clear = true;
    }
    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("LogCloseBtn"))) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        do_close = true;
    }

    if (do_clear) connection_log_clear(&state->conn_log);
    if (do_close) state->log_panel_open = false;
}
