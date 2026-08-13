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

/**
 * @brief Activate a chart series for the numeric value at line @p line_idx
 *        of the inspector's current pretty-printer output.
 *
 * Called by main.c when the user clicks the inline `+` button next to a
 * numeric leaf in the JSON view. Idempotent - no-op if (topic, dot_path)
 * already has an active series. No-op if the line is not numeric.
 */
void inspector_chart_add_from_line(AppState* state, int line_idx);

#endif
