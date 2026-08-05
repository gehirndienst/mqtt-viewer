// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <string.h>

#include "clay.h"
#include "raylib.h"

#include "ui/status_bar.h"
#include "ui/theme.h"

void status_bar_render(AppState* state) {
    static char stats[128] = "";
    static char sub_stats[3][128] = {"", "", ""};
    static float s_upd_timer = 0.0f;
    static int show_count = 0;

    s_upd_timer -= GetFrameTime();
    if (s_upd_timer <= 0.0f) {
        s_upd_timer = 1.0f;
        show_count = state->subscription_count < 3 ? state->subscription_count : 3;
        snprintf(stats, sizeof(stats), "%u topics \xc2\xb7 %lu msgs \xc2\xb7 %.1f msg/s", state->total_topics,
                 (unsigned long)state->total_messages, state->msgs_per_sec);
        for (int i = 0; i < show_count; i++) {
            Subscription* sub = &state->subscriptions[i];
            snprintf(sub_stats[i], sizeof(sub_stats[i]), " | sub %s: %llu msgs \xc2\xb7 %.1f/s", sub->topic_filter,
                     (unsigned long long)sub->message_count, sub->throughput);
        }
    }

    CLAY(CLAY_ID("StatusBar"),
         {
             .layout =
                 {
                     .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                     .padding = {12, 12, 4, 4},
                     .childGap = 8,
                     .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                 },
             .backgroundColor = THEME_BG_BAR,
             .border = {.width = {.top = 1}, .color = THEME_BORDER},
         }) {
        Clay_String stats_str = {.length = (int32_t)strlen(stats), .chars = stats};
        CLAY_TEXT(stats_str, THEME_TEXT_SMALL);

        if (show_count > 0) {
            for (int i = 0; i < show_count; i++) {
                char div_id[16];
                snprintf(div_id, sizeof(div_id), "SBDiv_%d", i);
                Clay_String div_id_cs = {.length = (int32_t)strlen(div_id), .chars = div_id};
                CLAY(CLAY_SID(div_id_cs),
                     {
                         .layout = {.sizing = {CLAY_SIZING_FIXED(1), CLAY_SIZING_FIXED(12)}},
                         .backgroundColor = THEME_BORDER,
                     }) {}
                Clay_String ss = {.length = (int32_t)strlen(sub_stats[i]), .chars = sub_stats[i]};
                CLAY_TEXT(ss, THEME_TEXT_SMALL);
            }
        }

        CLAY(CLAY_ID("StatusBarSpacer"), {.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) {}

        CLAY(CLAY_ID("PublishBtn"),
             {
                 .layout = {.padding = {8, 8, 3, 3}},
                 .backgroundColor = THEME_BG_BUTTON,
                 .cornerRadius = CLAY_CORNER_RADIUS(3),
             }) {
            CLAY_TEXT(CLAY_STRING("\xe2\x96\xb2 Publish"),
                      CLAY_TEXT_CONFIG({
                          .fontSize = 12,
                          .fontId = FONT_DEFAULT,
                          .textColor = THEME_TEXT_MUTED,
                      }));
        }
    }

    if (Clay_PointerOver(Clay_GetElementId(CLAY_STRING("PublishBtn"))) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        state->publish_panel_open = true;
    }
}
