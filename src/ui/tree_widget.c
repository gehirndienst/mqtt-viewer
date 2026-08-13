// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "clay.h"
#include "raylib.h"

#include "model/app_state.h"
#include "platform/db.h"
#include "ui/inspector_widget.h"
#include "ui/text_input.h"
#include "ui/theme.h"
#include "ui/tree_widget.h"

// Tree row indentation: base inset plus a fixed step per nesting level (px).
#define TREE_INDENT_BASE 16
#define TREE_INDENT_STEP 20

#define SEARCH_MAX_RESULTS 50

static MessageRecord s_search_results[SEARCH_MAX_RESULTS];
static int s_search_result_count = 0;
static int s_search_expanded_idx = -1; // which result row is expanded in place, -1 = none
static char s_last_search_query[256] = "";
static bool s_last_search_mode = false;
static char s_search_row_ids[SEARCH_MAX_RESULTS][32];
static char s_search_open_ids[SEARCH_MAX_RESULTS][32];
static char s_search_ts_bufs[SEARCH_MAX_RESULTS][16];
static char s_search_meta_bufs[SEARCH_MAX_RESULTS][32];

// Returns true if node's full path or any descendant's path contains filter
static bool node_matches_filter(TopicNode* node, const char* filter) {
    if (filter[0] == '\0') return true;

    char path[512];
    topic_node_full_path(node, path, sizeof(path));

    char lower_path[512];
    char lower_filter[256];
    size_t path_len = strlen(path);
    size_t filter_len = strlen(filter);

    for (size_t i = 0; i < path_len; i++)
        lower_path[i] = (char)((path[i] >= 'A' && path[i] <= 'Z') ? path[i] + 32 : path[i]);
    lower_path[path_len] = '\0';

    for (size_t i = 0; i < filter_len && i < 255; i++)
        lower_filter[i] = (char)((filter[i] >= 'A' && filter[i] <= 'Z') ? filter[i] + 32 : filter[i]);
    lower_filter[filter_len] = '\0';

    if (strstr(lower_path, lower_filter)) return true;

    for (uint32_t i = 0; i < node->child_count; i++) {
        if (node_matches_filter(node->children[i], filter)) return true;
    }
    return false;
}

static void render_node(AppState* state, TopicNode* node, int depth) {
    if (!node_matches_filter(node, state->topic_filter)) return;

    bool is_selected = (state->selected_topic == node);
    bool has_children = (node->child_count > 0);
    bool has_messages = (node->message_count > 0);

    // IDs for expand chevron and the row body (name + badge)
    char expand_id_buf[128];
    char row_id_buf[128];
    snprintf(expand_id_buf, sizeof(expand_id_buf), "exp_%p", (void*)node);
    snprintf(row_id_buf, sizeof(row_id_buf), "row_%p", (void*)node);
    Clay_String expand_id_str = {.length = (int32_t)strlen(expand_id_buf), .chars = expand_id_buf};
    Clay_String row_id_str = {.length = (int32_t)strlen(row_id_buf), .chars = row_id_buf};

    // Outer node container - used only for background/border styling
    char node_id_buf[128];
    snprintf(node_id_buf, sizeof(node_id_buf), "node_%p", (void*)node);
    Clay_String node_id_str = {.length = (int32_t)strlen(node_id_buf), .chars = node_id_buf};

    uint16_t left_pad = (uint16_t)(TREE_INDENT_BASE + depth * TREE_INDENT_STEP);

    CLAY(CLAY_SID(node_id_str), {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
            .padding = { 0, 12, 0, 0 },
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
        },
        .backgroundColor = is_selected ? THEME_BG_SELECTED : (Clay_Color) { 0 },
        .border = is_selected ? (Clay_BorderElementConfig) {
            .width = { .left = 3 }, .color = THEME_ACCENT_BLUE,
} : (Clay_BorderElementConfig) { 0 },
    }) {
        // [expand icon] · name · [R] · count badge
        {
            char r1[128];
            snprintf(r1, sizeof(r1), "r1_%p", (void*)node);
            Clay_String r1cs = {.length = (int32_t)strlen(r1), .chars = r1};
            CLAY(CLAY_SID(r1cs),
                 {
                     .layout =
                         {
                             .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                             .padding = {left_pad, 0, 5, 5},
                             .childGap = 6,
                             .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                         },
                 }) {
                // Expand chevron - visible box button, separate hit target
                if (has_children) {
                    CLAY(CLAY_SID(expand_id_str),
                         {
                             .layout =
                                 {
                                     .sizing = {CLAY_SIZING_FIXED(16), CLAY_SIZING_FIXED(16)},
                                     .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER},
                                 },
                             .backgroundColor = THEME_BG_BUTTON,
                             .cornerRadius = CLAY_CORNER_RADIUS(3),
                         }) {
                        CLAY_TEXT(node->expanded ? CLAY_STRING("v") : CLAY_STRING(">"),
                                  CLAY_TEXT_CONFIG({
                                      .fontSize = 10,
                                      .fontId = FONT_DEFAULT,
                                      .textColor = THEME_TEXT_SECONDARY,
                                  }));
                    }
                }
                // Row body - clicking opens inspector (only if node has messages)
                CLAY(CLAY_SID(row_id_str),
                     {
                         .layout =
                             {
                                 .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                                 .childGap = 6,
                                 .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                             },
                     }) {
                    Clay_String seg_str = {.length = (int32_t)strlen(node->segment), .chars = node->segment};
                    CLAY_TEXT(seg_str,
                              CLAY_TEXT_CONFIG({
                                  .fontSize = 14,
                                  .fontId = FONT_DEFAULT,
                                  .textColor = is_selected ? THEME_LIGHT_BLUE : THEME_TEXT_PRIMARY,
                              }));
                    if (node->has_retained) {
                        CLAY_TEXT(CLAY_STRING("R"),
                                  CLAY_TEXT_CONFIG({
                                      .fontSize = 10,
                                      .fontId = FONT_DEFAULT,
                                      .textColor = THEME_ORANGE,
                                  }));
                    }
                    if (has_messages) {
                        char cb[128];
                        snprintf(cb, sizeof(cb), "cb_%p", (void*)node);
                        Clay_String cbcs = {.length = (int32_t)strlen(cb), .chars = cb};
                        Clay_String cnt_str = {
                            .length = (int32_t)strlen(node->msg_count_str),
                            .chars = node->msg_count_str,
                        };
                        CLAY(CLAY_SID(cbcs),
                             {
                                 .layout = {.padding = {6, 6, 2, 2}},
                                 .backgroundColor = THEME_BG_BADGE,
                                 .cornerRadius = CLAY_CORNER_RADIUS(3),
                             }) {
                            CLAY_TEXT(cnt_str,
                                      CLAY_TEXT_CONFIG({
                                          .fontSize = 11,
                                          .fontId = FONT_MONO,
                                          .textColor = THEME_ACCENT_BLUE_BRIGHT,
                                      }));
                        }
                    }
                    // Subtree total badge- sum of messages on this node and every descendant
                    // Only meaningful for parents; for leaves it would duplicate the own-count badge
                    if (has_children && node->subtree_message_count > 0) {
                        char sb[128];
                        snprintf(sb, sizeof(sb), "sb_%p", (void*)node);
                        Clay_String sbcs = {.length = (int32_t)strlen(sb), .chars = sb};
                        Clay_String sum_str = {
                            .length = (int32_t)strlen(node->subtree_count_str),
                            .chars = node->subtree_count_str,
                        };
                        CLAY(CLAY_SID(sbcs),
                             {
                                 .layout = {.padding = {6, 6, 2, 2}},
                                 .backgroundColor = THEME_BG_BADGE,
                                 .cornerRadius = CLAY_CORNER_RADIUS(3),
                             }) {
                            CLAY_TEXT(sum_str,
                                      CLAY_TEXT_CONFIG({
                                          .fontSize = 11,
                                          .fontId = FONT_MONO,
                                          .textColor = THEME_LIGHT_BLUE,
                                      }));
                        }
                    }
                }
            }
        }
        // Row 2: payload preview - max 200 chars
        if (node->last_payload_preview[0] != '\0') {
            char r2[128];
            snprintf(r2, sizeof(r2), "r2_%p", (void*)node);
            Clay_String r2cs = {.length = (int32_t)strlen(r2), .chars = r2};

            const char* full = node->last_payload_preview;
            size_t full_len = strlen(full);
            size_t disp_len = full_len > 200 ? 200 : full_len;
            Clay_String prev_str = {.length = (int32_t)disp_len, .chars = full};
            uint16_t preview_pad = (uint16_t)(left_pad + (has_children ? 24 : 0));
            CLAY(CLAY_SID(r2cs),
                 {
                     .layout =
                         {
                             .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                             .padding = {preview_pad, 0, 0, 4},
                         },
                     .clip = {.horizontal = true},
                 }) {
                CLAY_TEXT(prev_str,
                          CLAY_TEXT_CONFIG({
                              .fontSize = 11,
                              .fontId = FONT_MONO,
                              .textColor = THEME_TEXT_DIM,
                              .wrapMode = CLAY_TEXT_WRAP_NONE,
                          }));
            }
        }
    }

    // Click detection
    bool tree_panel_hovered = Clay_PointerOver(Clay_GetElementId(CLAY_STRING("TreePanel")));
    bool node_hovered = Clay_PointerOver(Clay_GetElementId(node_id_str));
    if (tree_panel_hovered && node_hovered) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            // Expand chevron takes priority - check it first
            bool on_chevron = has_children && Clay_PointerOver(Clay_GetElementId(expand_id_str));
            if (on_chevron) {
                node->expanded = !node->expanded;
            } else if (has_messages) {
                // Anywhere else on the node row - open inspector
                state->selected_topic = node;
                state->inspector_frozen = false;
            } else if (has_children) {
                state->selected_topic = node;
                state->inspector_frozen = false;
                if (!node->expanded) node->expanded = true;
            }
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            state->context_menu_target = node;
            Vector2 mouse_pos = GetMousePosition();
            state->context_menu_x = mouse_pos.x;
            state->context_menu_y = mouse_pos.y;
            state->context_menu_open = true;
        }
    }

    if (has_children && node->expanded) {
        for (uint32_t i = 0; i < node->child_count; i++) {
            render_node(state, node->children[i], depth + 1);
        }
    }
}

static void render_search_results(AppState* state) {
    if (state->topic_filter[0] == '\0') {
        CLAY(CLAY_ID("SearchHint"),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .padding = {16, 16, 40, 40},
                         .childAlignment = {.x = CLAY_ALIGN_X_CENTER},
                     },
             }) {
            CLAY_TEXT(CLAY_STRING("Type to search all stored messages by topic or payload."), THEME_TEXT_SMALL);
        }
        return;
    }

    if (s_search_result_count == 0) {
        CLAY(CLAY_ID("SearchEmpty"),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .padding = {16, 16, 40, 40},
                         .childAlignment = {.x = CLAY_ALIGN_X_CENTER},
                     },
             }) {
            CLAY_TEXT(CLAY_STRING("No matches."), THEME_TEXT_SMALL);
        }
        return;
    }

    for (int i = 0; i < s_search_result_count; i++) {
        MessageRecord* r = &s_search_results[i];
        bool expanded = (i == s_search_expanded_idx);

        snprintf(s_search_row_ids[i], sizeof(s_search_row_ids[i]), "SR_%d", i);
        snprintf(s_search_open_ids[i], sizeof(s_search_open_ids[i]), "SROpen_%d", i);
        time_t secs = (time_t)(r->timestamp_us / 1000000ull);
        struct tm tm_info;
        localtime_r(&secs, &tm_info);
        strftime(s_search_ts_bufs[i], sizeof(s_search_ts_bufs[i]), "%H:%M:%S", &tm_info);
        snprintf(s_search_meta_bufs[i], sizeof(s_search_meta_bufs[i]), "Q%u%s", r->qos, r->retained ? " R" : "");

        Clay_String row_cs = {.length = (int32_t)strlen(s_search_row_ids[i]), .chars = s_search_row_ids[i]};
        Clay_String open_cs = {.length = (int32_t)strlen(s_search_open_ids[i]), .chars = s_search_open_ids[i]};
        Clay_String topic_cs = {.length = (int32_t)strlen(r->topic), .chars = r->topic};
        Clay_String meta_cs = {.length = (int32_t)strlen(s_search_meta_bufs[i]), .chars = s_search_meta_bufs[i]};
        Clay_String ts_cs = {.length = (int32_t)strlen(s_search_ts_bufs[i]), .chars = s_search_ts_bufs[i]};
        Clay_String prev_cs = {.length = (int32_t)strlen(r->preview), .chars = r->preview};

        char hdr_id[40], topic_wrap_id[40], prev_wrap_id[40];
        snprintf(hdr_id, sizeof(hdr_id), "SRHdr_%d", i);
        snprintf(topic_wrap_id, sizeof(topic_wrap_id), "SRTopic_%d", i);
        snprintf(prev_wrap_id, sizeof(prev_wrap_id), "SRPrev_%d", i);
        Clay_String hdr_cs = {.length = (int32_t)strlen(hdr_id), .chars = hdr_id};
        Clay_String topic_wrap_cs = {.length = (int32_t)strlen(topic_wrap_id), .chars = topic_wrap_id};
        Clay_String prev_wrap_cs = {.length = (int32_t)strlen(prev_wrap_id), .chars = prev_wrap_id};

        CLAY(CLAY_SID(row_cs),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,
                         .padding = {12, 12, 6, 6},
                         .childGap = 2,
                     },
                 .backgroundColor = expanded ? THEME_BG_HISTORY_EXPANDED : (Clay_Color){0},
                 .border = expanded ? (Clay_BorderElementConfig){.width = {.left = 3}, .color = THEME_ACCENT_BLUE}
                                    : (Clay_BorderElementConfig){0},
             }) {
            CLAY(CLAY_SID(hdr_cs),
                 {
                     .layout =
                         {
                             .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                             .childGap = 8,
                             .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                         },
                 }) {
                CLAY_TEXT(expanded ? CLAY_STRING("v") : CLAY_STRING(">"),
                          CLAY_TEXT_CONFIG({
                              .fontSize = 10,
                              .fontId = FONT_DEFAULT,
                              .textColor = expanded ? THEME_ACCENT_BLUE : THEME_TEXT_DIM,
                          }));
                CLAY(CLAY_SID(topic_wrap_cs),
                     {
                         .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}},
                     }) {
                    CLAY_TEXT(topic_cs,
                              CLAY_TEXT_CONFIG({
                                  .fontSize = 13,
                                  .fontId = FONT_DEFAULT,
                                  .textColor = expanded ? THEME_LIGHT_BLUE : THEME_TEXT_PRIMARY,
                              }));
                }
                CLAY_TEXT(meta_cs,
                          CLAY_TEXT_CONFIG({
                              .fontSize = 11,
                              .fontId = FONT_MONO,
                              .textColor = THEME_ACCENT_BLUE_BRIGHT,
                          }));
                CLAY_TEXT(ts_cs,
                          CLAY_TEXT_CONFIG({
                              .fontSize = 11,
                              .fontId = FONT_MONO,
                              .textColor = THEME_TEXT_DIM,
                          }));
                CLAY(CLAY_SID(open_cs),
                     {
                         .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .padding = {6, 6, 2, 2}},
                         .backgroundColor = THEME_BG_BUTTON,
                         .cornerRadius = CLAY_CORNER_RADIUS(3),
                     }) {
                    CLAY_TEXT(CLAY_STRING("Open Topic"),
                              CLAY_TEXT_CONFIG({
                                  .fontSize = 10,
                                  .fontId = FONT_DEFAULT,
                                  .textColor = THEME_TEXT_MUTED,
                              }));
                }
            }
            // a preview of THIS matched message: collapsed = one truncated line, expanded = full wrapped text
            CLAY(CLAY_SID(prev_wrap_cs),
                 {
                     .layout =
                         {
                             .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                             .padding = {18, 0, 0, 0},
                         },
                     .clip = {.horizontal = !expanded},
                 }) {
                CLAY_TEXT(prev_cs,
                          CLAY_TEXT_CONFIG({
                              .fontSize = 11,
                              .fontId = FONT_MONO,
                              .textColor = THEME_TEXT_DIM,
                              .wrapMode = expanded ? CLAY_TEXT_WRAP_WORDS : CLAY_TEXT_WRAP_NONE,
                          }));
            }
        }

        bool open_clicked = Clay_PointerOver(Clay_GetElementId(open_cs)) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        bool row_hovered = Clay_PointerOver(Clay_GetElementId(row_cs));
        if (open_clicked) {
            TopicNode* node = topic_tree_find(&state->topic_tree, r->topic);
            if (node) {
                state->selected_topic = node;
                state->inspector_frozen = false;
                state->inspector_view = VIEW_HISTORY;
            }
        } else if (row_hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            bool now_expanding = !expanded;
            s_search_expanded_idx = now_expanding ? i : -1;
            if (now_expanding) {
                state->frozen_message = *r; // payload is always NULL on search results - no ownership to copy
                state->inspector_frozen = true;
                state->selected_topic = NULL; // frozen and live inspector views are mutually exclusive
            } else {
                state->inspector_frozen = false;
            }
        }
    }
}

void tree_widget_render(AppState* state, Db* db) {
    if (state->topic_filter_focused && !state->publish_panel_open && !state->context_menu_open &&
        !state->profile_dialog_open) {
        if (text_input_select_all_pressed()) {
            state->topic_filter_all_selected = true;
        }

        if (!text_input_handle_copy(state->topic_filter)) {
            if (IsKeyPressed(KEY_BACKSPACE)) {
                if (state->topic_filter_all_selected) {
                    state->topic_filter[0] = '\0';
                } else if (strlen(state->topic_filter) > 0) {
                    state->topic_filter[strlen(state->topic_filter) - 1] = '\0';
                }
                state->topic_filter_all_selected = false;
            }

            if (IsKeyPressed(KEY_ESCAPE)) {
                state->topic_filter[0] = '\0';
                state->topic_filter_focused = false;
                state->topic_filter_all_selected = false;
            }

            int ch;
            while ((ch = GetCharPressed()) != 0) {
                if (state->topic_filter_all_selected) {
                    state->topic_filter[0] = '\0';
                    state->topic_filter_all_selected = false;
                }
                size_t len = strlen(state->topic_filter);
                if (ch >= 32 && ch < 127 && len < sizeof(state->topic_filter) - 1) {
                    state->topic_filter[len] = (char)ch;
                    state->topic_filter[len + 1] = '\0';
                }
            }

            if (state->topic_filter_all_selected && text_input_paste_pressed()) {
                state->topic_filter[0] = '\0'; // paste replaces the selection instead of appending to it
            }

            if (text_input_handle_paste(state->topic_filter, sizeof(state->topic_filter), false)) {
                state->topic_filter_all_selected = false;
            }
        }
    } else if (!state->publish_panel_open && !state->profile_dialog_open) {
        // Drain GetCharPressed so characters don't accumulate when no panel uses them
        while (GetCharPressed() != 0) {}
    }

    // Lose focus when another panel opens
    if (state->publish_panel_open || state->context_menu_open || state->profile_dialog_open) {
        state->topic_filter_focused = false;
        state->topic_filter_all_selected = false;
    }

    // Re-run the search only when the query text changed or we just switched into search mode
    bool search_mode = state->topic_filter_search_mode;
    if (search_mode && (!s_last_search_mode || strcmp(state->topic_filter, s_last_search_query) != 0)) {
        strncpy(s_last_search_query, state->topic_filter, sizeof(s_last_search_query) - 1);
        s_last_search_query[sizeof(s_last_search_query) - 1] = '\0';
        int n = db_search_messages(db, state->topic_filter, s_search_results, SEARCH_MAX_RESULTS);
        s_search_result_count = n > 0 ? n : 0;
        s_search_expanded_idx = -1; // stale index into the old result set otherwise
    }
    s_last_search_mode = search_mode;

    bool filter_selected = state->topic_filter_focused && state->topic_filter_all_selected;

    CLAY(CLAY_ID("TreeFilter"),
         {
             .layout =
                 {
                     .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                     .padding = {12, 12, 8, 8},
                 },
             .backgroundColor = THEME_BG_MAIN,
         }) {
        CLAY(CLAY_ID("FilterInput"),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .padding = {10, 6, 6, 6},
                         .childGap = 6,
                         .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                     },
                 .backgroundColor = filter_selected ? THEME_BG_INPUT_SELECTED : THEME_BG_INPUT,
                 .cornerRadius = CLAY_CORNER_RADIUS(4),
                 .border =
                     {
                         .width = CLAY_BORDER_OUTSIDE(1),
                         .color = state->topic_filter_focused ? THEME_ACCENT_BLUE : THEME_BORDER,
                     },
             }) {
            CLAY(CLAY_ID("FilterModeBtn"),
                 {
                     .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .padding = {6, 6, 2, 2}},
                     .backgroundColor = search_mode ? THEME_BG_SELECTED : THEME_BG_BUTTON,
                     .cornerRadius = CLAY_CORNER_RADIUS(3),
                 }) {
                CLAY_TEXT(search_mode ? CLAY_STRING("Messages") : CLAY_STRING("Topics"),
                          CLAY_TEXT_CONFIG({
                              .fontSize = 11,
                              .fontId = FONT_DEFAULT,
                              .textColor = search_mode ? THEME_TEXT_PRIMARY : THEME_TEXT_MUTED,
                          }));
            }
            CLAY(CLAY_ID("FilterText"),
                 {
                     .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}},
                 }) {
                if (state->topic_filter[0] != '\0') {
                    // filter text + cursor
                    static char filter_display[260];
                    size_t flen = strlen(state->topic_filter);
                    memcpy(filter_display, state->topic_filter, flen);
                    if (state->topic_filter_focused && !filter_selected) {
                        filter_display[flen] = '|';
                        filter_display[flen + 1] = '\0';
                    } else {
                        filter_display[flen] = '\0';
                    }
                    Clay_String fs = {.length = (int32_t)strlen(filter_display), .chars = filter_display};
                    CLAY_TEXT(fs,
                              CLAY_TEXT_CONFIG({
                                  .fontSize = 13,
                                  .fontId = FONT_DEFAULT,
                                  .textColor = THEME_TEXT_PRIMARY,
                              }));
                } else if (state->topic_filter_focused) {
                    CLAY_TEXT(CLAY_STRING("|"),
                              CLAY_TEXT_CONFIG({
                                  .fontSize = 13,
                                  .fontId = FONT_DEFAULT,
                                  .textColor = THEME_TEXT_PRIMARY,
                              }));
                } else {
                    CLAY_TEXT(search_mode ? CLAY_STRING("Search messages...") : CLAY_STRING("Filter topics..."),
                              THEME_TEXT_SMALL);
                }
            }
            if (state->topic_filter[0] != '\0') {
                CLAY(CLAY_ID("FilterClearBtn"),
                     {
                         .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .padding = {6, 6, 2, 2}},
                         .backgroundColor = THEME_BG_BUTTON,
                         .cornerRadius = CLAY_CORNER_RADIUS(3),
                     }) {
                    CLAY_TEXT(CLAY_STRING("\xc3\x97"),
                              CLAY_TEXT_CONFIG({
                                  .fontSize = 11,
                                  .fontId = FONT_DEFAULT,
                                  .textColor = THEME_TEXT_MUTED,
                              }));
                }
            }
        }
    }

    bool filter_hovered = Clay_PointerOver(Clay_GetElementId(CLAY_STRING("FilterInput")));
    bool clear_hovered =
        state->topic_filter[0] != '\0' && Clay_PointerOver(Clay_GetElementId(CLAY_STRING("FilterClearBtn")));
    bool mode_btn_hovered = Clay_PointerOver(Clay_GetElementId(CLAY_STRING("FilterModeBtn")));
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (mode_btn_hovered) {
            state->topic_filter_search_mode = !state->topic_filter_search_mode;
        } else if (clear_hovered) {
            state->topic_filter[0] = '\0';
            state->topic_filter_focused = false;
        } else {
            state->topic_filter_focused = filter_hovered;
        }
        state->topic_filter_all_selected = false;
    }

    CLAY(CLAY_ID("TreeScroll"),
         {
             .layout =
                 {
                     .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                 },
             .clip = {.vertical = true, .childOffset = Clay_GetScrollOffset()},
         }) {
        if (search_mode) {
            render_search_results(state);
        } else {
            TopicTree* tree = &state->topic_tree;
            for (uint32_t i = 0; i < tree->root_count; i++) {
                render_node(state, tree->roots[i], 0);
            }

            if (tree->root_count == 0) {
                CLAY(CLAY_ID("TreeEmpty"),
                     {
                         .layout =
                             {
                                 .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                                 .padding = {16, 16, 40, 40},
                                 .childAlignment = {.x = CLAY_ALIGN_X_CENTER},
                             },
                     }) {
                    CLAY_TEXT(CLAY_STRING("No topics yet. Connect to a broker."), THEME_TEXT_SMALL);
                }
            }
        }
    }
}
