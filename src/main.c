// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/mqtt_client.h"
#include "model/app_state.h"
#include "model/csv_export.h"
#include "model/message_buf.h"
#include "model/subscription.h"
#include "platform/db.h"
#include "platform/export_path.h"
#include "platform/log.h"
#include "platform/ui.h"
#include "ui/chart_panel.h"
#include "ui/context_menu.h"
#include "ui/inspector_widget.h"
#include "ui/log_panel.h"
#include "ui/menu_bar.h"
#include "ui/profile_dialog.h"
#include "ui/publish_panel.h"
#include "ui/status_bar.h"
#include "ui/theme.h"
#include "ui/tree_widget.h"

int main(void) {
    // NOTE: libmosquitto may write to a broker socket that is already closed (e.g. the TLS shutdown handshake right
    // after a user-initiated disconnect). macOS delivers SIGPIPE for that write, which kills the process with no
    // crash report. Errors are handled via the write's EPIPE return instead
    signal(SIGPIPE, SIG_IGN);

    // Raylib + font + Clay init
    UiCtx ui_ctx;
    if (!ui_init(&ui_ctx)) return 1;

    // App state
    AppState state;
    app_state_init(&state);

    // NOTE: history persistence tracks monotonic push counters, not the ring-buffer count - the ring saturates at
    // capacity (and is cleared on disconnect), so its count can't tell how many records were never written to the DB
    uint64_t history_pushed = 0; // records ever pushed to state.global_history
    uint64_t history_saved = 0; // records already written to the DB

    // Database
    char db_path[1280];
    db_resolve_path(db_path, sizeof(db_path));
    Db* db = db_open(db_path);
    if (db) {
        state.profile_count = db_load_profiles(db, state.profiles, 32);

        // Load UI settings
        // Restore the saved window size only if it fits on the current monitor's logical (point-sized) work area
        const char* w_str = db_get_setting(db, "window_width", NULL);
        const char* h_str = db_get_setting(db, "window_height", NULL);
        if (w_str && h_str) {
            int w = atoi(w_str), h = atoi(h_str);
            Vector2 dpi = GetWindowScaleDPI();
            float sx = dpi.x > 0.5f ? dpi.x : 1.0f;
            float sy = dpi.y > 0.5f ? dpi.y : 1.0f;
            int mon_w = (int)((float)GetMonitorWidth(GetCurrentMonitor()) / sx);
            int mon_h = (int)((float)GetMonitorHeight(GetCurrentMonitor()) / sy);
            if (mon_w > 60) mon_w -= 60; // OS chrome margin
            if (mon_h > 120) mon_h -= 120; // menu bar + dock
            if (w > mon_w) w = mon_w;
            if (h > mon_h) h = mon_h;
            if (w >= 900 && h >= 600) SetWindowSize(w, h);
        }
        const char* ratio_str = db_get_setting(db, "tree_width_ratio", NULL);
        if (ratio_str) {
            float r = (float)atof(ratio_str);
            if (r >= 0.1f && r <= 0.9f) state.tree_width_ratio = r;
        }

        // Load recent message history (last 200)
        static MessageRecord hist_records[200];
        int loaded = db_load_messages(db, hist_records, 200);
        for (int i = loaded - 1; i >= 0; i--) {
            message_buf_push(&state.global_history, &hist_records[i]);
            free(hist_records[i].payload);
            hist_records[i].payload = NULL;
            history_pushed++;
        }
        // loaded records came FROM the DB - don't write them back
        history_saved = history_pushed;
    }

    // MQTT client; a connection is initiated from the profile dialog
    MqttClient* mqtt = mqtt_client_new(&state.msg_queue, &state.conn_log);

    float save_timer = 0.0f;
    float frame_time_avg = 1.0f / 60.0f;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        // EWMA of frame time for UI refresh throttling
        frame_time_avg = frame_time_avg * 0.95f + dt * 0.05f;
        float ui_tick_seconds = 7.0f * frame_time_avg;
        if (ui_tick_seconds < 0.25f) ui_tick_seconds = 0.25f;
        uint64_t ui_tick_us = (uint64_t)(ui_tick_seconds * 1.0e6f);

        float width = (float)GetScreenWidth();
        float height = (float)GetScreenHeight();

        // update clay input
        Vector2 mouse = GetMousePosition();
        Clay_SetLayoutDimensions((Clay_Dimensions){.width = width, .height = height});
        Clay_SetPointerState((Clay_Vector2){.x = mouse.x, .y = mouse.y}, IsMouseButtonDown(MOUSE_BUTTON_LEFT));

        // NOTE: Clay's built-in routing picks the deepest hovered scroll container and silently drops the wheel if that
        // container doesn't actually overflow
        // if the cursor is anywhere inside the TreePanel bounding box, apply the wheel directly to TreeScroll's
        // scrollPosition and pass zero to Clay so it doesn't double-apply or reroute
        float wheel_raw = GetMouseWheelMove();
        Clay_Vector2 clay_scroll_delta = {.x = 0, .y = wheel_raw * 3.0f};

        // NOTE: Don't intercept wheel when profile dialog, publish panel, context menu is open above the tree -
        // they host their own scroll containers and need Clay's normal routing
        bool modal_open = state.profile_dialog_open || state.publish_panel_open || state.context_menu_open;
        if (wheel_raw != 0.0f && !modal_open) {
            Clay_ElementData panel = Clay_GetElementData(CLAY_ID("TreePanel"));
            if (panel.found) {
                Clay_BoundingBox b = panel.boundingBox;
                bool cursor_in_panel =
                    mouse.x >= b.x && mouse.x <= b.x + b.width && mouse.y >= b.y && mouse.y <= b.y + b.height;
                if (cursor_in_panel) {
                    Clay_ScrollContainerData sd =
                        Clay_GetScrollContainerData(Clay_GetElementId(CLAY_STRING("TreeScroll")));
                    if (sd.found && sd.scrollPosition) {
                        sd.scrollPosition->y += wheel_raw * 30.0f;
                        // clamp against viewport
                        float min_y = -(sd.contentDimensions.height - sd.scrollContainerDimensions.height);
                        if (min_y > 0.0f) min_y = 0.0f;
                        if (sd.scrollPosition->y > 0.0f) sd.scrollPosition->y = 0.0f;
                        if (sd.scrollPosition->y < min_y) sd.scrollPosition->y = min_y;
                    }
                    clay_scroll_delta.y = 0.0f;
                }
            }
        }

        Clay_UpdateScrollContainers(true, clay_scroll_delta, dt);

        // kb shortcuts
        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_P)) {
            if (!state.publish_panel_open) {
                if (state.selected_topic) {
                    topic_node_full_path(state.selected_topic, state.publish_topic, sizeof(state.publish_topic));
                }
                state.publish_panel_open = true;
            } else {
                state.publish_panel_open = false;
            }
        } else if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_L)) {
            state.log_panel_open = !state.log_panel_open;
        } else if (IsKeyPressed(KEY_P) && !state.profile_dialog_open && !state.publish_panel_open &&
                   !state.topic_filter_focused) {
            state.profile_dialog_open = !state.profile_dialog_open;
        }

        static float s_rate_acc = 0.0f;
        static float s_rate_timer = 0.0f;
        static float s_tpt_acc = 0.0f;
        static float s_tpt_timer = 0.0f;
        static TopicNode* s_tpt_node = NULL;

        MqttMessage msgs[256];
        uint32_t msg_count = spsc_queue_drain(&state.msg_queue, msgs, 256);
        for (uint32_t i = 0; i < msg_count; i++) {
            MqttMessage* m = &msgs[i];
            TopicNode* node = topic_tree_insert(&state.topic_tree, m->topic);
            if (node) {
                topic_node_count_message(node);
                node->last_message_ts = m->timestamp_us;

                for (TopicNode* anc = node; anc; anc = anc->parent) {
                    if (anc->subtree_message_count == 1 ||
                        (m->timestamp_us - anc->last_subtree_display_update_us) >= ui_tick_us) {
                        anc->last_subtree_display_update_us = m->timestamp_us;
                        snprintf(anc->subtree_count_str, sizeof(anc->subtree_count_str), "\xce\xa3 %u",
                                 anc->subtree_message_count);
                    }
                }
                if (node == state.selected_topic) s_tpt_acc += 1.0f;
                node->last_payload_len = m->payload_len;
                node->last_qos = m->qos;
                node->has_retained = m->retained || node->has_retained;

                // NOTE: refresh the display strings only every `ui_tick_us`. Each refresh is a Clay text-cache miss, so
                // for a fast topic this collapses to a lot. First message always refreshes
                if (node->message_count == 1 || (m->timestamp_us - node->last_display_update_us) >= ui_tick_us) {
                    node->last_display_update_us = m->timestamp_us;
                    snprintf(node->msg_count_str, sizeof(node->msg_count_str), "%u", node->message_count);
                    if (m->payload) {
                        // 4 bytes for optional "..." terminator if truncated
                        bool truncated = (m->payload_len > TOPIC_PREVIEW_LEN - 4);
                        uint32_t node_plen = truncated ? TOPIC_PREVIEW_LEN - 4 : m->payload_len;
                        for (uint32_t pi = 0; pi < node_plen; pi++) {
                            unsigned char c = (unsigned char)m->payload[pi];
                            node->last_payload_preview[pi] = (c < 0x20 || c == 0x7f) ? ' ' : (char)c;
                        }

                        // Collapse consecutive spaces in-place; drop leading/trailing
                        uint32_t src = 0, dst = 0;
                        while (src < node_plen) {
                            if (node->last_payload_preview[src] == ' ') {
                                if (dst > 0 && node->last_payload_preview[dst - 1] != ' ')
                                    node->last_payload_preview[dst++] = ' ';
                                src++;
                            } else {
                                node->last_payload_preview[dst++] = node->last_payload_preview[src++];
                            }
                        }
                        if (dst > 0 && node->last_payload_preview[dst - 1] == ' ') dst--;
                        if (truncated) {
                            if (dst > TOPIC_PREVIEW_LEN - 4) dst = TOPIC_PREVIEW_LEN - 4;
                            node->last_payload_preview[dst] = '.';
                            node->last_payload_preview[dst + 1] = '.';
                            node->last_payload_preview[dst + 2] = '.';
                            node->last_payload_preview[dst + 3] = '\0';
                        } else {
                            node->last_payload_preview[dst] = '\0';
                        }
                    } else {
                        // zero-length payload (e.g. "clear retained") must not leave a stale preview
                        node->last_payload_preview[0] = '\0';
                    }
                }

                MessageRecord rec = {
                    .timestamp_us = m->timestamp_us,
                    .payload = m->payload,
                    .payload_len = m->payload_len,
                    .qos = m->qos,
                    .retained = m->retained,
                    .broker_id = (state.active_profile_idx >= 0 && state.active_profile_idx < state.profile_count)
                        ? (uint32_t)state.profiles[state.active_profile_idx].id
                        : 0,
                };

                // Fill short preview for history view
                if (m->payload) {
                    uint32_t prev_len = m->payload_len < MSG_PREVIEW_LEN - 1 ? m->payload_len : MSG_PREVIEW_LEN - 1;
                    for (uint32_t pi = 0; pi < prev_len; pi++) {
                        unsigned char c = (unsigned char)m->payload[pi];
                        rec.preview[pi] = (c < 0x20 || c == 0x7f) ? ' ' : (char)c;
                    }
                    rec.preview[prev_len] = '\0';
                }
                topic_node_full_path(node, rec.topic, sizeof(rec.topic));
                message_buf_push(&state.global_history, &rec);
                history_pushed++;

                // chart sample capture - must happen before m->payload is freed
                chart_panel_capture_sample(&state, rec.topic, m->payload, m->payload_len, m->timestamp_us);

                free(m->payload);
                m->payload = NULL;

                // track per-subscription stats
                for (int si = 0; si < state.subscription_count; si++) {
                    Subscription* sub = &state.subscriptions[si];
                    if (mqtt_topic_matches(sub->topic_filter, rec.topic)) {
                        sub->message_count++;
                        sub->bytes_total += rec.payload_len;
                        // Sliding window for throughput
                        if (sub->window_count < 128) {
                            uint32_t wi = (sub->window_head + sub->window_count) % 128;
                            sub->window_timestamps[wi] = rec.timestamp_us;
                            sub->window_count++;
                        } else {
                            sub->window_timestamps[sub->window_head] = rec.timestamp_us;
                            sub->window_head = (sub->window_head + 1) % 128;
                        }
                    }
                }
                state.total_messages++;
            } else {
                free(m->payload);
            }
        }
        state.total_topics = topic_tree_count(&state.topic_tree);

        s_rate_acc += (float)msg_count;
        s_rate_timer += dt;
        if (s_rate_timer >= 1.0f) {
            state.msgs_per_sec = s_rate_acc / s_rate_timer;
            s_rate_acc = 0.0f;
            s_rate_timer = 0.0f;
        }
        if (state.selected_topic != s_tpt_node) {
            s_tpt_node = state.selected_topic;
            s_tpt_acc = 0.0f;
            s_tpt_timer = 0.0f;
            if (s_tpt_node) s_tpt_node->throughput = 0.0f;
        }
        s_tpt_timer += dt;
        if (s_tpt_timer >= 1.0f) {
            if (s_tpt_node) s_tpt_node->throughput = s_tpt_acc / s_tpt_timer;
            s_tpt_acc = 0.0f;
            s_tpt_timer = 0.0f;
        }

        // handle disconnect request
        if (state.disconnect_requested) {
            state.disconnect_requested = false;
            mqtt_client_disconnect(mqtt);
            topic_tree_destroy(&state.topic_tree);
            topic_tree_init(&state.topic_tree, TOPIC_TREE_CAPACITY);
            message_buf_clear(&state.global_history);
            history_saved = history_pushed; // cleared records are gone - nothing left to save
            state.selected_topic = NULL;
            s_tpt_node = NULL;
            s_tpt_acc = 0.0f;
            s_tpt_timer = 0.0f;
            state.total_topics = 0;
            state.total_messages = 0;
            state.topic_filter[0] = '\0';
            state.topic_filter_focused = false;
            for (int si = 0; si < state.subscription_count; si++) {
                state.subscriptions[si].message_count = 0;
                state.subscriptions[si].bytes_total = 0;
                state.subscriptions[si].throughput = 0.0f;
                state.subscriptions[si].window_head = 0;
                state.subscriptions[si].window_count = 0;
            }
            state.profile_dialog_open = true;
        }

        // handle CSV export request (set by the export dialog)
        if (state.export_requested) {
            state.export_requested = false;
            char log_msg[1400];
            FILE* f = fopen(state.export_path, "wb");
            if (!f) {
                snprintf(log_msg, sizeof(log_msg), "CSV export failed: %s", strerror(errno));
                connection_log_add(&state.conn_log, CONN_LOG_ERROR, log_msg);
                LOG_ERROR("csv export: fopen(%s): %s", state.export_path, strerror(errno));
            } else {
                int64_t rows = csv_export_write(&state.global_history, state.export_topic, f);
                bool close_ok = fclose(f) == 0;
                if (rows < 0 || !close_ok) {
                    snprintf(log_msg, sizeof(log_msg), "CSV export failed: %s", strerror(errno));
                    connection_log_add(&state.conn_log, CONN_LOG_ERROR, log_msg);
                    LOG_ERROR("csv export: write to %s failed", state.export_path);
                } else {
                    snprintf(log_msg, sizeof(log_msg), "Exported %lld messages to %s", (long long)rows,
                             state.export_path);
                    connection_log_add(&state.conn_log, CONN_LOG_INFO, log_msg);
                }
            }
        }

        if (mqtt_client_is_connected(mqtt)) {
            state.conn_state = CONN_CONNECTED;
        } else if (mqtt_client_is_connecting(mqtt)) {
            state.conn_state = CONN_CONNECTING;
        } else if (mqtt_client_last_disconnect_rc(mqtt) != 0) {
            state.conn_state = CONN_RECONNECTING;
        } else {
            state.conn_state = CONN_DISCONNECTED;
        }

        // rolling throughput for each subscription
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        double now_sec = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
        for (int si = 0; si < state.subscription_count; si++) {
            Subscription* sub = &state.subscriptions[si];
            uint32_t in_window = 0;
            for (uint32_t wi = 0; wi < sub->window_count; wi++) {
                uint32_t idx = (sub->window_head + wi) % 128;
                double ts_sec = (double)sub->window_timestamps[idx] / 1e6;
                if (now_sec - ts_sec <= 10.0) in_window++;
            }
            sub->throughput = (float)in_window / 10.0f;
        }

        // NOTE: Periodic db flush: save new messages every 5 seconds. Trim runs alongside save so each call deletes
        // ~256 rows (one save batch's worth) - keeping per-call delete work bounded
        save_timer += dt;
        if (db && save_timer >= 5.0f) {
            save_timer = 0.0f;
            // window_start = monotonic index of the oldest record still in the ring
            uint64_t window_start = history_pushed - message_buf_count(&state.global_history);
            if (history_saved < window_start) history_saved = window_start;
            if (history_saved < history_pushed) {
                while (history_saved < history_pushed) {
                    uint32_t new_count = (uint32_t)(history_pushed - history_saved);
                    if (new_count > 256) new_count = 256;
                    static MessageRecord batch[256];
                    uint32_t start = (uint32_t)(history_saved - window_start);
                    for (uint32_t i = 0; i < new_count; i++) {
                        const MessageRecord* r = message_buf_get(&state.global_history, start + i);
                        if (r) batch[i] = *r;
                    }
                    db_save_messages(db, batch, (int)new_count);
                    history_saved += new_count;
                }
                db_trim_messages(db, 10000);
            }
        }

        // build layout
        Clay_BeginLayout();

        CLAY(CLAY_ID("Root"),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     },
                 .backgroundColor = THEME_BG_MAIN,
             }) {
            menu_bar_render(&state);
            CLAY(CLAY_ID("Content"),
                 {
                     .layout =
                         {
                             .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                         },
                 }) {
                // Left column: tree on top, chart panel stacked underneath when at least one series is active
                CLAY(CLAY_ID("LeftColumn"),
                     {
                         .layout =
                             {
                                 .sizing =
                                     {
                                         state.selected_topic ? CLAY_SIZING_PERCENT(state.tree_width_ratio)
                                                              : CLAY_SIZING_GROW(0),
                                         CLAY_SIZING_GROW(0),
                                     },
                                 .layoutDirection = CLAY_TOP_TO_BOTTOM,
                             },
                     }) {
                    CLAY(CLAY_ID("TreePanel"),
                         {
                             .layout =
                                 {
                                     .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                                 },
                         }) {
                        tree_widget_render(&state);
                    }
                    chart_panel_render(&state);
                }

                if (state.selected_topic) {
                    inspector_widget_render(&state);
                }
            }
            log_panel_render(&state);
            status_bar_render(&state);
        }

        context_menu_render(&state);
        profile_dialog_render(&state, db, mqtt);
        publish_panel_render(&state, mqtt);

        Clay_RenderCommandArray cmds = Clay_EndLayout(dt);

        // inspector close button - checked AFTER Clay_EndLayout so bounding box is current-frame
        if (state.selected_topic && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Clay_ElementData close_data = Clay_GetElementData(CLAY_ID("InspectorCloseBtn"));
            if (close_data.found) {
                Clay_BoundingBox b = close_data.boundingBox;
                if (mouse.x >= b.x && mouse.x <= b.x + b.width && mouse.y >= b.y && mouse.y <= b.y + b.height) {
                    state.selected_topic = NULL;
                }
            }
            Clay_ElementData diff_data = Clay_GetElementData(CLAY_ID("InspectorDiffToggle"));
            if (diff_data.found) {
                Clay_BoundingBox b = diff_data.boundingBox;
                if (mouse.x >= b.x && mouse.x <= b.x + b.width && mouse.y >= b.y && mouse.y <= b.y + b.height) {
                    state.diff_enabled = !state.diff_enabled;
                }
            }
            Clay_ElementData export_data = Clay_GetElementData(CLAY_ID("InspectorExportBtn"));
            if (state.selected_topic && export_data.found) {
                Clay_BoundingBox b = export_data.boundingBox;
                if (mouse.x >= b.x && mouse.x <= b.x + b.width && mouse.y >= b.y && mouse.y <= b.y + b.height) {
                    topic_node_full_path(state.selected_topic, state.export_topic, sizeof(state.export_topic));
                    if (export_path_resolve(state.export_topic, state.export_path, sizeof(state.export_path))) {
                        state.export_requested = true;
                    } else {
                        connection_log_add(&state.conn_log, CONN_LOG_ERROR,
                                           "CSV export failed: no writable destination directory");
                    }
                }
            }

            // Chart [+] inline buttons next to numeric values in the JSON view. scan for every line index - only the
            // ones whose Clay element exists this frame have a hit
            for (int li = 0; li < 2048; li++) {
                char id[24];
                snprintf(id, sizeof(id), "ChartAdd_%d", li);
                Clay_String idcs = {.length = (int32_t)strlen(id), .chars = id};
                Clay_ElementData ed = Clay_GetElementData(Clay_GetElementId(idcs));
                if (!ed.found) continue;
                Clay_BoundingBox b = ed.boundingBox;
                if (mouse.x < b.x || mouse.x > b.x + b.width || mouse.y < b.y || mouse.y > b.y + b.height) continue;
                inspector_chart_add_from_line(&state, li);
                break;
            }
            // Chart [x] remove buttons in the ChartsPanel (one per active series)
            for (int si = 0; si < CHART_MAX_SERIES; si++) {
                if (!state.chart_series[si].active) continue;
                char id[24];
                snprintf(id, sizeof(id), "ChartRm_%d", si);
                Clay_String idcs = {.length = (int32_t)strlen(id), .chars = id};
                Clay_ElementData ed = Clay_GetElementData(Clay_GetElementId(idcs));
                if (!ed.found) continue;
                Clay_BoundingBox b = ed.boundingBox;
                if (mouse.x < b.x || mouse.x > b.x + b.width || mouse.y < b.y || mouse.y > b.y + b.height) continue;
                chart_series_reset(&state.chart_series[si]);
            }
        }

        // Render
        BeginDrawing();
        ClearBackground((Color){26, 26, 26, 255});

        // NOTE: Raylib's BeginScissorMode replaces (not nests) the current scissor. Clay emits nested SCISSOR_START/END
        // pairs (e.g. the horizontal preview clip inside the tree scroll container). Without a stack, EndScissorMode on
        // the inner clip destroys the outer clip, letting scrolled tree items bleed above the filter bar
        // so scissor stack stores the ACTIVE scissor at each depth and reapplies the correct one on each SCISSOR_END
        Clay_BoundingBox scissor_stack[8];
        int scissor_depth = 0;

        for (int32_t i = 0; i < cmds.length; i++) {
            Clay_RenderCommand* cmd = cmds.internalArray + i;
            Clay_BoundingBox bb = cmd->boundingBox;

            switch (cmd->commandType) {
                case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
                    Clay_Color c = cmd->renderData.rectangle.backgroundColor;
                    float radius = cmd->renderData.rectangle.cornerRadius.topLeft;
                    if (radius > 0) {
                        float norm = radius / (bb.width < bb.height ? bb.width : bb.height);
                        DrawRectangleRounded(
                            (Rectangle){bb.x, bb.y, bb.width, bb.height}, norm, 8,
                            (Color){(unsigned char)c.r, (unsigned char)c.g, (unsigned char)c.b, (unsigned char)c.a});
                    } else {
                        DrawRectangle(
                            (int)bb.x, (int)bb.y, (int)bb.width, (int)bb.height,
                            (Color){(unsigned char)c.r, (unsigned char)c.g, (unsigned char)c.b, (unsigned char)c.a});
                    }
                    break;
                }
                case CLAY_RENDER_COMMAND_TYPE_TEXT: {
                    Clay_TextRenderData* td = &cmd->renderData.text;
                    Font font = ui_get_font(td->fontId);
                    static char text_buf[4096];
                    uint32_t len = (uint32_t)td->stringContents.length;
                    if (len >= sizeof(text_buf)) {
                        len = (uint32_t)(sizeof(text_buf) - 1);
                    }
                    memcpy(text_buf, td->stringContents.chars, len);
                    text_buf[len] = '\0';
                    DrawTextEx(font, text_buf, (Vector2){bb.x, bb.y}, (float)td->fontSize, 1.0f,
                               (Color){(unsigned char)td->textColor.r, (unsigned char)td->textColor.g,
                                       (unsigned char)td->textColor.b, (unsigned char)td->textColor.a});
                    break;
                }
                case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
                    Clay_BoundingBox clipped = bb;
                    if (scissor_depth > 0) {
                        Clay_BoundingBox outer = scissor_stack[scissor_depth - 1];
                        float x1 = outer.x > bb.x ? outer.x : bb.x;
                        float y1 = outer.y > bb.y ? outer.y : bb.y;
                        float x2 =
                            (outer.x + outer.width) < (bb.x + bb.width) ? (outer.x + outer.width) : (bb.x + bb.width);
                        float y2 = (outer.y + outer.height) < (bb.y + bb.height) ? (outer.y + outer.height)
                                                                                 : (bb.y + bb.height);
                        clipped.x = x1;
                        clipped.y = y1;
                        clipped.width = x2 > x1 ? x2 - x1 : 0.0f;
                        clipped.height = y2 > y1 ? y2 - y1 : 0.0f;
                    }
                    if (scissor_depth < 8) scissor_stack[scissor_depth++] = clipped;
                    BeginScissorMode((int)clipped.x, (int)clipped.y, (int)clipped.width, (int)clipped.height);
                    break;
                }
                case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
                    if (scissor_depth > 0) scissor_depth--;
                    if (scissor_depth > 0) {
                        Clay_BoundingBox s = scissor_stack[scissor_depth - 1];
                        BeginScissorMode((int)s.x, (int)s.y, (int)s.width, (int)s.height);
                    } else {
                        EndScissorMode();
                    }
                    break;
                case CLAY_RENDER_COMMAND_TYPE_BORDER: {
                    Clay_BorderRenderData* bd = &cmd->renderData.border;
                    Color col = (Color){(unsigned char)bd->color.r, (unsigned char)bd->color.g,
                                        (unsigned char)bd->color.b, (unsigned char)bd->color.a};
                    if (bd->width.top > 0) DrawRectangle((int)bb.x, (int)bb.y, (int)bb.width, (int)bd->width.top, col);
                    if (bd->width.bottom > 0)
                        DrawRectangle((int)bb.x, (int)(bb.y + bb.height - bd->width.bottom), (int)bb.width,
                                      (int)bd->width.bottom, col);
                    if (bd->width.left > 0)
                        DrawRectangle((int)bb.x, (int)bb.y, (int)bd->width.left, (int)bb.height, col);
                    if (bd->width.right > 0)
                        DrawRectangle((int)(bb.x + bb.width - bd->width.right), (int)bb.y, (int)bd->width.right,
                                      (int)bb.height, col);
                    break;
                }
                default:
                    break;
            }
        }

        chart_panel_draw(&state);

        EndDrawing();
    }

    mqtt_client_disconnect(mqtt);
    mqtt_client_destroy(mqtt);

    // persist ui settings
    if (db) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", GetScreenWidth());
        db_set_setting(db, "window_width", buf);
        snprintf(buf, sizeof(buf), "%d", GetScreenHeight());
        db_set_setting(db, "window_height", buf);
        snprintf(buf, sizeof(buf), "%.4f", state.tree_width_ratio);
        db_set_setting(db, "tree_width_ratio", buf);

        uint64_t window_start = history_pushed - message_buf_count(&state.global_history);
        if (history_saved < window_start) history_saved = window_start;
        if (history_saved < history_pushed) {
            while (history_saved < history_pushed) {
                uint32_t new_count = (uint32_t)(history_pushed - history_saved);
                if (new_count > 256) new_count = 256;
                static MessageRecord flush_batch[256];
                uint32_t start = (uint32_t)(history_saved - window_start);
                for (uint32_t i = 0; i < new_count; i++) {
                    const MessageRecord* r = message_buf_get(&state.global_history, start + i);
                    if (r) flush_batch[i] = *r;
                }
                db_save_messages(db, flush_batch, (int)new_count);
                history_saved += new_count;
            }
            db_trim_messages(db, 10000);
        }
        db_close(db);
    }

    app_state_destroy(&state);
    ui_shutdown(&ui_ctx);

    return 0;
}
