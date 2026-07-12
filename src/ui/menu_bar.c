// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <string.h>

#include "clay.h"
#include "raylib.h"

#include "ui/menu_bar.h"
#include "ui/theme.h"

void menu_bar_render(AppState* state) {
    // throttle broker address string to prevent reflow on every frame
    static char broker_buf[280] = "";
    static float s_upd_timer = 0.0f;
    s_upd_timer -= GetFrameTime();
    if (s_upd_timer <= 0.0f) {
        s_upd_timer = 0.5f;
        if (state->conn_state == CONN_CONNECTED)
            snprintf(broker_buf, sizeof(broker_buf), "%s:%u", state->broker_host, (unsigned)state->broker_port);
        else
            broker_buf[0] = '\0';
    }
    if (state->conn_state != CONN_CONNECTED) broker_buf[0] = '\0';

    CLAY(CLAY_ID("MenuBar"),
         {
             .layout =
                 {
                     .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                     .padding = {12, 12, 6, 6},
                     .childGap = 16,
                     .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                 },
             .backgroundColor = THEME_BG_BAR,
             .border = {.width = {.bottom = 1}, .color = THEME_BORDER},
         }) {
        CLAY(CLAY_ID("MenuConnectionBtn"),
             {
                 .layout = {.padding = {8, 8, 4, 4}},
                 .backgroundColor = state->profile_dialog_open ? THEME_BG_SELECTED : THEME_BG_BUTTON,
                 .cornerRadius = CLAY_CORNER_RADIUS(3),
             }) {
            CLAY_TEXT(CLAY_STRING("Connection"),
                      CLAY_TEXT_CONFIG({
                          .fontSize = 14,
                          .fontId = FONT_DEFAULT,
                          .textColor = state->profile_dialog_open ? THEME_TEXT_PRIMARY : THEME_TEXT_SECONDARY,
                      }));
        }
        CLAY(CLAY_ID("MenuLogBtn"),
             {
                 .layout = {.padding = {8, 8, 4, 4}},
                 .backgroundColor = state->log_panel_open ? THEME_BG_SELECTED : THEME_BG_BUTTON,
                 .cornerRadius = CLAY_CORNER_RADIUS(3),
             }) {
            CLAY_TEXT(CLAY_STRING("Log"),
                      CLAY_TEXT_CONFIG({
                          .fontSize = 14,
                          .fontId = FONT_DEFAULT,
                          .textColor = state->log_panel_open ? THEME_TEXT_PRIMARY : THEME_TEXT_SECONDARY,
                      }));
        }

        CLAY(CLAY_ID("MenuBarSpacer"), {.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) {}

        if (broker_buf[0] != '\0') {
            Clay_String bs = {.length = (int32_t)strlen(broker_buf), .chars = broker_buf};
            CLAY_TEXT(bs,
                      CLAY_TEXT_CONFIG({
                          .fontSize = 12,
                          .fontId = FONT_MONO,
                          .textColor = THEME_TEXT_DIM,
                      }));
        }
        if (state->conn_state == CONN_CONNECTED) {
            CLAY_TEXT(CLAY_STRING("\xc2\xb7 Connected"),
                      CLAY_TEXT_CONFIG({
                          .fontSize = 12,
                          .fontId = FONT_DEFAULT,
                          .textColor = THEME_GREEN,
                      }));
        } else if (state->conn_state == CONN_CONNECTING || state->conn_state == CONN_RECONNECTING) {
            CLAY_TEXT(CLAY_STRING("\xc2\xb7 Connecting..."),
                      CLAY_TEXT_CONFIG({
                          .fontSize = 12,
                          .fontId = FONT_DEFAULT,
                          .textColor = THEME_ORANGE,
                      }));
        } else {
            CLAY_TEXT(CLAY_STRING("\xc2\xb7 Disconnected"),
                      CLAY_TEXT_CONFIG({
                          .fontSize = 12,
                          .fontId = FONT_DEFAULT,
                          .textColor = THEME_TEXT_MUTED,
                      }));
        }

        if (state->conn_state != CONN_DISCONNECTED) {
            CLAY(CLAY_ID("MenuDisconnectBtn"),
                 {
                     .layout = {.padding = {8, 8, 4, 4}},
                     .backgroundColor = THEME_BG_BUTTON,
                     .cornerRadius = CLAY_CORNER_RADIUS(3),
                 }) {
                CLAY_TEXT(CLAY_STRING("Disconnect"),
                          CLAY_TEXT_CONFIG({
                              .fontSize = 14,
                              .fontId = FONT_DEFAULT,
                              .textColor = THEME_TEXT_SECONDARY,
                          }));
            }
        }
    }

    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("MenuLogBtn"))) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        state->log_panel_open = !state->log_panel_open;
    }
    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("MenuConnectionBtn"))) &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        state->profile_dialog_open = !state->profile_dialog_open;
    }
    if (state->conn_state != CONN_DISCONNECTED &&
        Clay_PointerOver(Clay_GetElementId(CLAY_STRING("MenuDisconnectBtn"))) &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        state->disconnect_requested = true;
    }
}
