// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef LOG_PANEL_H
#define LOG_PANEL_H

#include "model/app_state.h"

/**
 * @brief Render the connection log panel (fixed 200 px height in layout tree).
 *
 * Shows log entries newest-first with timestamp, severity badge (INFO/WARN/ERR),
 * and message text. Provides Clear and Close buttons.
 * Visible only when state->log_panel_open is true.
 */
void log_panel_render(AppState* state);

#endif
