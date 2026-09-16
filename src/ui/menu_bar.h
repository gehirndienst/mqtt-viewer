// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
/**
 * @file
 * @brief Top menu bar: connection status and the Log panel toggle
 */
#ifndef MENU_BAR_H
#define MENU_BAR_H

#include "model/app_state.h"

/**
 * @brief Render the top menu bar with connection status and Log toggle.
 *
 * Detects click on the Log button and flips state->log_panel_open.
 * Must be called inside an active Clay layout pass.
 */
void menu_bar_render(AppState* state);

#endif
