// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "clay.h"
#include "raylib.h"

#include "platform/update_check.h"
#include "ui/about_dialog.h"
#include "ui/theme.h"
#include "ui/ui_util.h"

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

#define APP_AUTHOR "Nikita Smirnov"
#define APP_REPO_URL "https://github.com/gehirndienst/mqtt-viewer"
#define APP_RELEASES_URL APP_REPO_URL "/releases/latest"

static void about_button(Clay_ElementId id, const char* label) {
    bool hovered = Clay_PointerOver(id);
    CLAY(id,
         {
             .layout = {.padding = {10, 10, 5, 5}},
             .backgroundColor = hovered ? THEME_BG_BTN_HOVER : THEME_BG_BUTTON,
             .cornerRadius = CLAY_CORNER_RADIUS(3),
         }) {
        CLAY_TEXT(ui_utils_clay_string(label), THEME_TEXT_BODY);
    }
}

// Rows are keyed by slot index: the same name under one parent would collide
static void about_row(uint32_t i, const char* key, const char* value) {
    CLAY(CLAY_IDI("AboutRow", i),
         {
             .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childGap = 12},
         }) {
        CLAY(CLAY_IDI("AboutKey", i), {.layout = {.sizing = {CLAY_SIZING_FIXED(80), CLAY_SIZING_FIT(0)}}}) {
            CLAY_TEXT(ui_utils_clay_string(key), THEME_TEXT_SMALL);
        }
        CLAY_TEXT(ui_utils_clay_string(value), THEME_TEXT_BODY);
    }
}

void about_dialog_render(AppState* state) {
    static bool s_was_open = false;
    if (!state->about_dialog_open) {
        s_was_open = false;
        return;
    }
    bool just_opened = !s_was_open;
    s_was_open = true;

    static char s_version[64];
    static char s_status[160];
    UpdateResult upd = update_check_poll();
    snprintf(s_version, sizeof(s_version), "%s", APP_VERSION);
    switch (upd.status) {
        case UPDATE_CHECKING:
            snprintf(s_status, sizeof(s_status), "Checking...");
            break;
        case UPDATE_UP_TO_DATE:
            snprintf(s_status, sizeof(s_status), "You are up to date (latest release: %s)", upd.latest);
            break;
        case UPDATE_AVAILABLE:
            snprintf(s_status, sizeof(s_status), "Update available: %s", upd.latest);
            break;
        case UPDATE_FAILED:
            snprintf(s_status, sizeof(s_status), "%s", upd.error);
            break;
        case UPDATE_IDLE:
        default:
            s_status[0] = '\0';
            break;
    }
    Clay_Color status_color = upd.status == UPDATE_FAILED ? THEME_TEXT_DANGER
        : upd.status == UPDATE_AVAILABLE                  ? THEME_GREEN
                                                          : THEME_TEXT_MUTED;

    bool repo_hovered = Clay_PointerOver(CLAY_ID("AboutRepoLink"));

    CLAY(CLAY_ID("AboutOverlay"),
         {
             .layout =
                 {
                     .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                     .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER},
                 },
             .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 110},
             .backgroundColor = THEME_BG_OVERLAY,
         }) {
        CLAY(CLAY_ID("AboutModal"),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_FIXED(440), CLAY_SIZING_FIT(0)},
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     },
                 .backgroundColor = THEME_BG_PANEL,
                 .cornerRadius = CLAY_CORNER_RADIUS(6),
                 .border = {.width = CLAY_BORDER_OUTSIDE(1), .color = THEME_BORDER},
             }) {
            CLAY(CLAY_ID("AboutTitleBar"),
                 {
                     .layout =
                         {
                             .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                             .padding = {16, 16, 12, 12},
                             .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                         },
                     .border = {.width = {.bottom = 1}, .color = THEME_BORDER},
                 }) {
                CLAY_TEXT(CLAY_STRING("About MQTT Viewer"), THEME_TEXT_HEADING);
                CLAY(CLAY_ID("AboutTitleSpacer"), {.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) {}
                CLAY(CLAY_ID("AboutCloseBtn"),
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

            CLAY(CLAY_ID("AboutBody"),
                 {
                     .layout =
                         {
                             .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                             .padding = CLAY_PADDING_ALL(16),
                             .childGap = 8,
                             .layoutDirection = CLAY_TOP_TO_BOTTOM,
                         },
                 }) {
                about_row(0, "Version", s_version);
                about_row(1, "Built", __DATE__);
                about_row(2, "Author", APP_AUTHOR);
                CLAY(CLAY_ID("AboutRepoRow"),
                     {.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childGap = 12}}) {
                    CLAY(CLAY_ID("AboutRepoKey"), {.layout = {.sizing = {CLAY_SIZING_FIXED(80), CLAY_SIZING_FIT(0)}}}) {
                        CLAY_TEXT(CLAY_STRING("Source"), THEME_TEXT_SMALL);
                    }
                    CLAY(CLAY_ID("AboutRepoLink"), {.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}}}) {
                        CLAY_TEXT(CLAY_STRING(APP_REPO_URL),
                                  CLAY_TEXT_CONFIG({
                                      .fontSize = 14,
                                      .fontId = FONT_DEFAULT,
                                      .textColor = repo_hovered ? THEME_ACCENT_BLUE_BRIGHT : THEME_ACCENT_BLUE,
                                  }));
                    }
                }

                CLAY(CLAY_ID("AboutUpdateRow"),
                     {
                         .layout =
                             {
                                 .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                                 .padding = {0, 0, 8, 0},
                                 .childGap = 10,
                                 .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                             },
                     }) {
                    about_button(CLAY_ID("AboutCheckBtn"), "Check for updates");
                    if (upd.status == UPDATE_AVAILABLE)
                        about_button(CLAY_ID("AboutOpenReleaseBtn"), "Open release page");
                }
                if (s_status[0] != '\0') {
                    CLAY_TEXT(ui_utils_clay_string(s_status),
                              CLAY_TEXT_CONFIG({
                                  .fontSize = 13,
                                  .fontId = FONT_DEFAULT,
                                  .textColor = status_color,
                                  .wrapMode = CLAY_TEXT_WRAP_WORDS,
                              }));
                }
            }
        }
    }

    if (!just_opened && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (Clay_PointerOver(CLAY_ID("AboutCloseBtn")) || !Clay_PointerOver(CLAY_ID("AboutModal"))) {
            state->about_dialog_open = false;
        } else if (Clay_PointerOver(CLAY_ID("AboutCheckBtn"))) {
            update_check_start(APP_VERSION);
        } else if (upd.status == UPDATE_AVAILABLE && Clay_PointerOver(CLAY_ID("AboutOpenReleaseBtn"))) {
            OpenURL(APP_RELEASES_URL);
        } else if (Clay_PointerOver(CLAY_ID("AboutRepoLink"))) {
            OpenURL(APP_REPO_URL);
        }
    }

    if (IsKeyPressed(KEY_ESCAPE)) state->about_dialog_open = false;
}
