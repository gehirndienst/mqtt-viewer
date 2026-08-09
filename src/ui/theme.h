// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef THEME_H
#define THEME_H

#include "clay.h"

// Background colors
#define THEME_BG_MAIN (Clay_Color){26, 26, 26, 255} // #1a1a1a
#define THEME_BG_PANEL (Clay_Color){30, 30, 30, 255} // #1e1e1e
#define THEME_BG_BAR (Clay_Color){42, 42, 42, 255} // #2a2a2a
#define THEME_BG_SUBTLE (Clay_Color){34, 34, 34, 255} // #222222
#define THEME_BG_INPUT (Clay_Color){42, 42, 42, 255} // #2a2a2a
#define THEME_BG_INPUT_ACTIVE (Clay_Color){35, 48, 68, 255} // focused/active input field
#define THEME_BG_INPUT_SELECTED (Clay_Color){70, 70, 130, 255} // #464682 - whole field text selected (Ctrl+A)
#define THEME_BG_TOGGLE_ON (Clay_Color){20, 50, 20, 255} // enabled toggle background
#define THEME_BG_OVERLAY (Clay_Color){0, 0, 0, 180} // modal backdrop scrim
#define THEME_BG_BUTTON (Clay_Color){51, 51, 51, 255} // #333333
#define THEME_BG_BTN_PRI (Clay_Color){58, 90, 58, 255} // #3a5a3a
#define THEME_BG_SELECTED (Clay_Color){42, 42, 74, 255} // #2a2a4a
#define THEME_BG_HOVER (Clay_Color){58, 58, 90, 255} // #3a3a5a

// Text colors
#define THEME_TEXT_PRIMARY (Clay_Color){255, 255, 255, 255} // #ffffff
#define THEME_TEXT_SECONDARY (Clay_Color){204, 204, 204, 255} // #cccccc
#define THEME_TEXT_MUTED (Clay_Color){136, 136, 136, 255} // #888888
#define THEME_TEXT_DIM (Clay_Color){102, 102, 102, 255} // #666666

// Accent colors
#define THEME_ACCENT_BLUE (Clay_Color){102, 102, 255, 255} // #6666ff
#define THEME_ACCENT_BLUE_BRIGHT (Clay_Color){200, 200, 255, 255} // #c8c8ff - badge text on dark badge bg
#define THEME_GREEN (Clay_Color){102, 204, 102, 255} // #66cc66
#define THEME_ORANGE (Clay_Color){255, 136, 0, 255} // #ff8800
#define THEME_CYAN (Clay_Color){0, 170, 255, 255} // #00aaff
#define THEME_PINK (Clay_Color){255, 102, 170, 255} // #ff66aa
#define THEME_LIGHT_BLUE (Clay_Color){153, 204, 255, 255} // #99ccff
#define THEME_RED (Clay_Color){255, 102, 102, 255} // #ff6666

// Border
#define THEME_BORDER (Clay_Color){68, 68, 68, 255} // #444444
#define THEME_BORDER_SUBTLE (Clay_Color){45, 45, 45, 255} // #2d2d2d — faint row separator

// Semantic widget backgrounds
#define THEME_BG_PLOT (Clay_Color){20, 22, 30, 255} // chart plot reservation
#define THEME_BG_BADGE (Clay_Color){35, 55, 90, 255} // topic message-count badge
#define THEME_BG_HISTORY_EXPANDED (Clay_Color){30, 35, 50, 255} // expanded history row
#define THEME_BG_HISTORY_DETAIL (Clay_Color){20, 24, 36, 255} // expanded history detail
#define THEME_BG_LATEST (Clay_Color){26, 26, 42, 255} // latest-value section
#define THEME_BG_BTN_HOVER (Clay_Color){58, 58, 75, 255} // hovered secondary button
#define THEME_BG_FLASH_OK (Clay_Color){20, 55, 20, 255} // save-confirmation flash

// Danger (destructive action) palette
#define THEME_BG_DANGER (Clay_Color){80, 30, 30, 255}
#define THEME_BG_DANGER_HOVER (Clay_Color){120, 45, 45, 255}
#define THEME_BORDER_DANGER (Clay_Color){180, 70, 70, 255}
#define THEME_TEXT_DANGER (Clay_Color){220, 130, 130, 255}

// Diff highlight (added vs previous payload / removed vs previous payload)
#define THEME_DIFF_ADDED (Clay_Color){80, 200, 80, 255} // #50c850
#define THEME_DIFF_REMOVED (Clay_Color){220, 80, 80, 255} // #dc5050
#define THEME_DIFF_BG_ADDED (Clay_Color){34, 60, 34, 255} // #223c22
#define THEME_DIFF_BG_REMOVED (Clay_Color){60, 34, 34, 255} // #3c2222

// Font IDs
#define FONT_DEFAULT 0
#define FONT_MONO 1

// Common text configs
#define THEME_TEXT_BODY CLAY_TEXT_CONFIG({.fontSize = 14, .fontId = FONT_DEFAULT, .textColor = THEME_TEXT_SECONDARY})
#define THEME_TEXT_HEADING CLAY_TEXT_CONFIG({.fontSize = 16, .fontId = FONT_DEFAULT, .textColor = THEME_TEXT_PRIMARY})
#define THEME_TEXT_SMALL CLAY_TEXT_CONFIG({.fontSize = 12, .fontId = FONT_DEFAULT, .textColor = THEME_TEXT_MUTED})
#define THEME_TEXT_MONO CLAY_TEXT_CONFIG({.fontSize = 13, .fontId = FONT_MONO, .textColor = THEME_TEXT_SECONDARY})

#endif
