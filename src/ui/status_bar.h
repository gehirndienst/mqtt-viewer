// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef STATUS_BAR_H
#define STATUS_BAR_H

#include "model/app_state.h"

/**
 * @brief Render the bottom status bar showing topic/message counts and the
 *        Publish shortcut button.
 *
 * Must be called inside an active Clay layout pass.
 */
void status_bar_render(AppState* state);

#endif
