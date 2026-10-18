// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
/**
 * @file
 * @brief Modal About dialog: version, build date, author and a manual update check
 */
#ifndef ABOUT_DIALOG_H
#define ABOUT_DIALOG_H

#include "model/app_state.h"

/**
 * @brief Render the About modal when AppState::about_dialog_open is set.
 *
 * Esc, the close button or a click outside the modal closes it. The update check runs only when the user presses
 * "Check for updates". Must be called inside an active Clay layout pass.
 */
void about_dialog_render(AppState* state);

#endif
