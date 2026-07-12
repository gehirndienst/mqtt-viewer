// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "clay.h"
#include "raylib.h"

#include "model/connection_log.h"
#include "ui/publish_panel.h"
#include "ui/theme.h"

#define PP_BUF_COUNT 32
#define PP_BUF_SIZE 512

static char s_pp_bufs[PP_BUF_COUNT][PP_BUF_SIZE];
static int s_pp_buf_idx = PP_BUF_COUNT - 1;

static char* pp_next_buf(void) {
    s_pp_buf_idx = (s_pp_buf_idx + 1) % PP_BUF_COUNT;
    return s_pp_bufs[s_pp_buf_idx];
}

void publish_panel_render(AppState* state, MqttClient* mqtt) {
    if (!state->publish_panel_open) return;

    s_pp_buf_idx = PP_BUF_COUNT - 1;

    static bool s_was_open = false;
    if (!s_was_open) {
        state->publish_active_field = 0; // reset to topic field when panel first opens
    }
    s_was_open = true;

    bool do_publish = false;
    bool do_close = false;
    int set_qos = -1; // -1 = no change
    bool toggle_retain = false;
    bool click_topic = false;
    bool click_payload = false;

    {
        int ch;
        while ((ch = GetCharPressed()) != 0) {
            if (state->publish_active_field == 0) {
                size_t len = strlen(state->publish_topic);
                if (len + 1 < sizeof(state->publish_topic)) {
                    state->publish_topic[len] = (char)ch;
                    state->publish_topic[len + 1] = '\0';
                }
            } else {
                size_t len = strlen(state->publish_payload);
                if (len + 1 < sizeof(state->publish_payload)) {
                    state->publish_payload[len] = (char)ch;
                    state->publish_payload[len + 1] = '\0';
                }
            }
        }
        bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
            if (ctrl) {
                if (state->publish_active_field == 0)
                    state->publish_topic[0] = '\0';
                else
                    state->publish_payload[0] = '\0';
            } else {
                if (state->publish_active_field == 0) {
                    size_t len = strlen(state->publish_topic);
                    if (len > 0) state->publish_topic[len - 1] = '\0';
                } else {
                    size_t len = strlen(state->publish_payload);
                    if (len > 0) state->publish_payload[len - 1] = '\0';
                }
            }
        }
        if (IsKeyPressed(KEY_ESCAPE)) {
            do_close = true;
        }
    }

    // Topic field value
    static char s_topic_display[256 + 4];
    snprintf(s_topic_display, sizeof(s_topic_display), "%s%s", state->publish_topic,
             state->publish_active_field == 0 ? "|" : "");

    // Payload field value only show first 128 chars to avoid huge display
    static char s_payload_display[PP_BUF_SIZE];
    {
        size_t pay_len = strlen(state->publish_payload);
        size_t show = pay_len < (PP_BUF_SIZE - 4) ? pay_len : (PP_BUF_SIZE - 4);
        memcpy(s_payload_display, state->publish_payload, show);
        if (pay_len > show) {
            s_payload_display[show] = '.';
            s_payload_display[show + 1] = '.';
            s_payload_display[show + 2] = '.';
            s_payload_display[show + 3] = '\0';
        } else {
            s_payload_display[show] = '\0';
        }
        if (state->publish_active_field == 1) {
            size_t cur = strlen(s_payload_display);
            if (cur + 1 < sizeof(s_payload_display)) {
                s_payload_display[cur] = '|';
                s_payload_display[cur + 1] = '\0';
            }
        }
    }

    static const char* s_qos_labels[3] = {"0", "1", "2"};
    static const char* s_qos_ids[3] = {"PPQos0", "PPQos1", "PPQos2"};

    static char s_retain_display[8];
    snprintf(s_retain_display, sizeof(s_retain_display), state->publish_retain ? "[X]" : "[ ]");

    CLAY(CLAY_ID("PublishPanel"),
         {
             .layout =
                 {
                     .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .padding = {12, 12, 10, 10},
                     .childGap = 8,
                 },
             .backgroundColor = THEME_BG_PANEL,
             .border = {.width = {.top = 1}, .color = THEME_BORDER},
             .floating =
                 {
                     .attachTo = CLAY_ATTACH_TO_ROOT,
                     .zIndex = 50,
                     .attachPoints =
                         {
                             .element = CLAY_ATTACH_POINT_LEFT_BOTTOM,
                             .parent = CLAY_ATTACH_POINT_LEFT_BOTTOM,
                         },
                 },
         }) {
        // Title row
        CLAY(CLAY_ID("PPTitleRow"),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .childGap = 8,
                         .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                     },
             }) {
            CLAY_TEXT(CLAY_STRING("Publish Message"), THEME_TEXT_HEADING);
            CLAY(CLAY_ID("PPTitleSpacer"),
                 {
                     .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}},
                 }) {}
            // Close button
            CLAY(CLAY_ID("PPCloseBtn"),
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

        // Topic row
        CLAY(CLAY_ID("PPTopicRow"),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .childGap = 8,
                         .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                     },
             }) {
            // Label
            CLAY(CLAY_ID("PPTopicLbl"),
                 {
                     .layout = {.sizing = {CLAY_SIZING_PERCENT(0.10f), CLAY_SIZING_FIT(0)}},
                 }) {
                CLAY_TEXT(CLAY_STRING("Topic"), THEME_TEXT_SMALL);
            }
            // Input box
            Clay_String topic_cs = {
                .length = (int32_t)strlen(s_topic_display),
                .chars = s_topic_display,
            };
            CLAY(CLAY_ID("PPTopicInput"),
                 {
                     .layout =
                         {
                             .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(28, 0)},
                             .padding = {8, 8, 4, 4},
                         },
                     .backgroundColor = state->publish_active_field == 0 ? THEME_BG_HOVER : THEME_BG_INPUT,
                     .cornerRadius = CLAY_CORNER_RADIUS(3),
                     .border = {.width = CLAY_BORDER_OUTSIDE(1), .color = THEME_BORDER},
                 }) {
                CLAY_TEXT(topic_cs, THEME_TEXT_MONO);
            }
        }

        // Payload row
        CLAY(CLAY_ID("PPPayloadRow"),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .childGap = 8,
                         .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                     },
             }) {
            CLAY(CLAY_ID("PPPayloadLbl"),
                 {
                     .layout = {.sizing = {CLAY_SIZING_PERCENT(0.10f), CLAY_SIZING_FIT(0)}},
                 }) {
                CLAY_TEXT(CLAY_STRING("Payload"), THEME_TEXT_SMALL);
            }
            Clay_String payload_cs = {
                .length = (int32_t)strlen(s_payload_display),
                .chars = s_payload_display,
            };
            CLAY(CLAY_ID("PPPayloadInput"),
                 {
                     .layout =
                         {
                             .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(72, 0)},
                             .padding = {8, 8, 4, 4},
                         },
                     .backgroundColor = state->publish_active_field == 1 ? THEME_BG_HOVER : THEME_BG_INPUT,
                     .cornerRadius = CLAY_CORNER_RADIUS(3),
                     .border = {.width = CLAY_BORDER_OUTSIDE(1), .color = THEME_BORDER},
                 }) {
                CLAY_TEXT(payload_cs, THEME_TEXT_MONO);
            }
        }

        // Controls row: QoS / Retain / Publish / Close
        CLAY(CLAY_ID("PPControlsRow"),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .childGap = 8,
                         .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                     },
             }) {
            // QoS label
            CLAY_TEXT(CLAY_STRING("QoS:"), THEME_TEXT_SMALL);

            // QoS 0 / 1 / 2 toggle buttons
            for (int q = 0; q < 3; q++) {
                bool active_qos = ((int)state->publish_qos == q);
                char* qid_buf = pp_next_buf();
                strncpy(qid_buf, s_qos_ids[q], PP_BUF_SIZE - 1);
                qid_buf[PP_BUF_SIZE - 1] = '\0';
                Clay_String qid_cs = {.length = (int32_t)strlen(qid_buf), .chars = qid_buf};
                CLAY(CLAY_SID(qid_cs),
                     {
                         .layout = {.padding = {10, 10, 4, 4}},
                         .backgroundColor = active_qos ? THEME_BG_SELECTED : THEME_BG_BUTTON,
                         .cornerRadius = CLAY_CORNER_RADIUS(3),
                     }) {
                    Clay_String ql = {
                        .length = (int32_t)strlen(s_qos_labels[q]),
                        .chars = s_qos_labels[q],
                    };
                    CLAY_TEXT(ql, THEME_TEXT_SMALL);
                }
            }

            // Retain toggle
            CLAY_TEXT(CLAY_STRING("Retain:"), THEME_TEXT_SMALL);
            Clay_String retain_cs = {
                .length = (int32_t)strlen(s_retain_display),
                .chars = s_retain_display,
            };
            CLAY(CLAY_ID("PPRetainBtn"),
                 {
                     .layout = {.padding = {8, 8, 4, 4}},
                     .backgroundColor = state->publish_retain ? THEME_BG_SELECTED : THEME_BG_BUTTON,
                     .cornerRadius = CLAY_CORNER_RADIUS(3),
                 }) {
                CLAY_TEXT(retain_cs, THEME_TEXT_SMALL);
            }

            // Spacer
            CLAY(CLAY_ID("PPCtrlSpacer"),
                 {
                     .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}},
                 }) {}

            // Publish button
            CLAY(CLAY_ID("PPPublishBtn"),
                 {
                     .layout = {.padding = {16, 16, 6, 6}},
                     .backgroundColor = THEME_BG_BTN_PRI,
                     .cornerRadius = CLAY_CORNER_RADIUS(4),
                 }) {
                CLAY_TEXT(CLAY_STRING("Publish"),
                          CLAY_TEXT_CONFIG({
                              .fontSize = 14,
                              .fontId = FONT_DEFAULT,
                              .textColor = THEME_TEXT_PRIMARY,
                          }));
            }
        }
    }

    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("PPCloseBtn"))) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        do_close = true;
    }
    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("PPTopicInput"))) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        click_topic = true;
    }
    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("PPPayloadInput"))) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        click_payload = true;
    }
    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("PPRetainBtn"))) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        toggle_retain = true;
    }
    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("PPPublishBtn"))) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        do_publish = true;
    }
    // QoS buttons
    for (int q = 0; q < 3; q++) {
        Clay_String qid_cs = {
            .length = (int32_t)strlen(s_qos_ids[q]),
            .chars = s_qos_ids[q],
        };
        if (Clay_PointerOver(Clay_GetElementId(qid_cs)) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            set_qos = q;
        }
    }

    if (click_topic) state->publish_active_field = 0;
    if (click_payload) state->publish_active_field = 1;
    if (toggle_retain) state->publish_retain = !state->publish_retain;
    if (set_qos >= 0) state->publish_qos = (uint8_t)set_qos;
    if (do_close) {
        state->publish_panel_open = false;
        s_was_open = false;
    }
    if (do_publish) {
        size_t pay_len = strlen(state->publish_payload);
        bool ok = mqtt_client_publish(mqtt, state->publish_topic, state->publish_payload, (uint32_t)pay_len,
                                      state->publish_qos, state->publish_retain);

        static char log_msg[512];
        if (ok) {
            snprintf(log_msg, sizeof(log_msg), "Published to '%s' (QoS %d, retain=%d, len=%u)", state->publish_topic,
                     (int)state->publish_qos, (int)state->publish_retain, (unsigned)pay_len);
            connection_log_add(&state->conn_log, CONN_LOG_INFO, log_msg);
        } else {
            snprintf(log_msg, sizeof(log_msg), "Publish failed for topic '%s'", state->publish_topic);
            connection_log_add(&state->conn_log, CONN_LOG_ERROR, log_msg);
        }
        state->publish_panel_open = false;
        s_was_open = false;
    }
}
