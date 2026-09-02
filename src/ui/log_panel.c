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
static uint32_t s_row_entry_idx[LOG_VISIBLE_ROWS];
static uint32_t s_row_count;
static float s_copied_timer;
static bool s_dragging; // left button went down on a row and has not been released yet
static char s_copy_buf[LOG_VISIBLE_ROWS * 300];

static const char* level_label_for(LogLevel level) {
    switch (level) {
        case CONN_LOG_WARN:
            return "WARN";
        case CONN_LOG_ERROR:
            return "ERR";
        case CONN_LOG_INFO:
        default:
            return "INFO";
    }
}

// Logical log index of the row under the mouse, or -1 when the pointer is not over a row
static int log_row_under_pointer(void) {
    for (uint32_t di = 0; di < s_row_count; di++) {
        Clay_String row_cs = {.length = (int32_t)strlen(s_row_ids[di]), .chars = s_row_ids[di]};
        if (Clay_PointerOver(Clay_GetElementId(row_cs))) return (int)s_row_entry_idx[di];
    }
    return -1;
}

static void log_copy_range(ConnectionLog* log, int lo, int hi) {
    if (lo < 0 || hi < lo) return;

    size_t used = 0;
    for (int idx = lo; idx <= hi; idx++) {
        LogEntry entry;
        if (!connection_log_get(log, (uint32_t)idx, &entry)) continue;

        time_t secs = (time_t)(entry.timestamp_us / 1000000u);
        struct tm tm_info;
        localtime_r(&secs, &tm_info);
        char ts[16];
        strftime(ts, sizeof(ts), "%H:%M:%S", &tm_info);

        int written = snprintf(s_copy_buf + used, sizeof(s_copy_buf) - used, "%s %s %s\n", ts,
                               level_label_for(entry.level), entry.message);
        if (written < 0) break;
        if ((size_t)written >= sizeof(s_copy_buf) - used) {
            used = sizeof(s_copy_buf) - 1;
            break;
        }
        used += (size_t)written;
    }
    if (used == 0) return;

    s_copy_buf[used] = '\0';
    SetClipboardText(s_copy_buf);
    s_copied_timer = 1.5f;
}

void log_panel_render(AppState* state) {
    if (!state->log_panel_open) return;

    uint32_t count = connection_log_count(&state->conn_log);

    bool do_clear = false;
    bool do_close = false;
    bool do_copy = false;

    if (s_copied_timer > 0.0f) s_copied_timer -= GetFrameTime();

    int sel_lo = -1, sel_hi = -1;
    if (state->log_sel_anchor >= 0 && state->log_sel_cursor >= 0) {
        sel_lo = state->log_sel_anchor < state->log_sel_cursor ? state->log_sel_anchor : state->log_sel_cursor;
        sel_hi = state->log_sel_anchor < state->log_sel_cursor ? state->log_sel_cursor : state->log_sel_anchor;
    }

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

            CLAY(CLAY_ID("LogCopyBtn"),
                 {
                     .layout = {.padding = {10, 10, 4, 4}},
                     .backgroundColor = THEME_BG_BUTTON,
                     .cornerRadius = CLAY_CORNER_RADIUS(3),
                 }) {
                CLAY_TEXT(s_copied_timer > 0.0f ? CLAY_STRING("Copied") : CLAY_STRING("Copy"), THEME_TEXT_SMALL);
            }

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
            s_row_count = display_count;

            // Iterate newest-first: from count-1 down to count-display_count
            for (uint32_t di = 0; di < display_count; di++) {
                uint32_t entry_idx = count - 1u - di;

                LogEntry entry;
                if (!connection_log_get(&state->conn_log, entry_idx, &entry)) continue;

                s_row_entry_idx[di] = entry_idx;
                bool row_selected = (sel_lo >= 0 && (int)entry_idx >= sel_lo && (int)entry_idx <= sel_hi);

                snprintf(s_row_ids[di], sizeof(s_row_ids[di]), "LogRow_%u", di);

                time_t secs = (time_t)(entry.timestamp_us / 1000000u);
                struct tm tm_info;
                localtime_r(&secs, &tm_info);
                strftime(s_log_bufs[di], sizeof(s_log_bufs[di]), "%H:%M:%S", &tm_info);

                Clay_Color level_color;
                switch (entry.level) {
                    case CONN_LOG_WARN:
                        level_color = THEME_ORANGE;
                        break;
                    case CONN_LOG_ERROR:
                        level_color = THEME_RED;
                        break;
                    case CONN_LOG_INFO:
                    default:
                        level_color = THEME_ACCENT_BLUE;
                        break;
                }

                // Pad to 4 chars so the messages line up under each other
                snprintf(s_level_bufs[di], sizeof(s_level_bufs[di]), "%-4s", level_label_for(entry.level));
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
                         .backgroundColor = row_selected ? THEME_BG_SELECTED : (Clay_Color){0},
                         .cornerRadius = CLAY_CORNER_RADIUS(2),
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

    bool clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    bool over_panel = Clay_PointerOver(Clay_GetElementId(CLAY_STRING("LogPanel")));
    if (clicked) state->log_focused = over_panel;

    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("LogCopyBtn"))) && clicked) {
        do_copy = true;
    }
    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("LogClearBtn"))) && clicked) {
        do_clear = true;
    }
    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("LogCloseBtn"))) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        do_close = true;
    }

    // Row clicks: plain click anchors a new selection, shift-click moves the far end
    if (clicked && over_panel) {
        int idx = log_row_under_pointer();
        if (idx >= 0) {
            bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            if (shift && state->log_sel_anchor >= 0) {
                state->log_sel_cursor = idx;
            } else {
                state->log_sel_anchor = idx;
                state->log_sel_cursor = idx;
            }
            s_dragging = true; // holding and moving now extends the selection
        } else if (!Clay_PointerOver(Clay_GetElementId(CLAY_STRING("LogHeader")))) {
            // Clicking the panel's empty area (not a row, not a button) drops the selection
            state->log_sel_anchor = -1;
            state->log_sel_cursor = -1;
        }
    }

    // Drag across rows with the button held
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        s_dragging = false;
    } else if (s_dragging) {
        int idx = log_row_under_pointer();
        if (idx >= 0) state->log_sel_cursor = idx;
    }

    // Keyboard only when the log owns it - otherwise Ctrl+A would fight the text fields' select-all
    bool keyboard_ours =
        state->log_focused && !state->profile_dialog_open && !state->publish_panel_open && !state->topic_filter_focused;
    if (keyboard_ours && s_row_count > 0) {
        bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) || IsKeyDown(KEY_LEFT_SUPER) ||
            IsKeyDown(KEY_RIGHT_SUPER);
        if (ctrl && IsKeyPressed(KEY_A)) {
            state->log_sel_anchor = (int)s_row_entry_idx[s_row_count - 1]; // oldest rendered row
            state->log_sel_cursor = (int)s_row_entry_idx[0]; // newest rendered row
        } else if (ctrl && IsKeyPressed(KEY_C)) {
            do_copy = true;
        } else if (IsKeyPressed(KEY_ESCAPE)) {
            state->log_sel_anchor = -1;
            state->log_sel_cursor = -1;
        }
    }

    if (do_copy) {
        if (state->log_sel_anchor >= 0 && state->log_sel_cursor >= 0) {
            int lo = state->log_sel_anchor < state->log_sel_cursor ? state->log_sel_anchor : state->log_sel_cursor;
            int hi = state->log_sel_anchor < state->log_sel_cursor ? state->log_sel_cursor : state->log_sel_anchor;
            log_copy_range(&state->conn_log, lo, hi);
        } else if (s_row_count > 0) {
            // Nothing selected - copy everything on screen
            log_copy_range(&state->conn_log, (int)s_row_entry_idx[s_row_count - 1], (int)s_row_entry_idx[0]);
        }
    }

    if (do_clear) {
        connection_log_clear(&state->conn_log);
        state->log_sel_anchor = -1;
        state->log_sel_cursor = -1;
    }
    if (do_close) state->log_panel_open = false;
}
