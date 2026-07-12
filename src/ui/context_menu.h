// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef CONTEXT_MENU_H
#define CONTEXT_MENU_H

#include "model/app_state.h"

/**
 * @brief Render the right-click context menu for a topic tree node.
 *
 * Items: Copy Topic Path, Publish Here, Expand All, Collapse All,
 * Clear History. Dismisses on Escape or click-outside.
 * Must be called inside an active Clay layout pass at zIndex >= 200.
 */
void context_menu_render(AppState* state);

#endif
