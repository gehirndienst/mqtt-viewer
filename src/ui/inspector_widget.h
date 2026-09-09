// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef INSPECTOR_WIDGET_H
#define INSPECTOR_WIDGET_H

#include <stdbool.h>
#include <stdint.h>

#include "model/app_state.h"

typedef enum {
    VIEW_JSON,
    VIEW_TEXT,
    VIEW_HEX,
    VIEW_HISTORY,
} ViewMode;

/**
 * @brief Render the message inspector panel for the selected topic.
 *
 * Shows payload content with Text/JSON/Hex tabs. Renders an empty state when
 * no topic is selected. Must be called inside an active Clay layout pass.
 */
void inspector_widget_render(AppState* state);

#endif
