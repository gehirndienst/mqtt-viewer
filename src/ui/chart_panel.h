// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef CHART_PANEL_H
#define CHART_PANEL_H

#include <stdbool.h>

#include "model/app_state.h"

/**
 * @brief Render the panel that hosts time-series plots, stacked under the
 *        topic tree in the left column. No-op when no series are active.
 *
 * Reserves a sized rectangle per active ChartSeries with a stable Clay ID
 * (`Plot_<i>`); the actual line plot is painted by chart_panel_draw() after
 * Clay_EndLayout, using raylib primitives looked up via Clay_GetElementData.
 *
 * Must be called inside an active Clay layout pass.
 */
void chart_panel_render(AppState* state);

/**
 * @brief Returns true if at least one ChartSeries is active. Used by the
 *        layout to decide whether to allocate space for the panel.
 */
bool chart_panel_has_active(const AppState* state);

/**
 * @brief Paint the line plots into the rectangles reserved by the previous
 *        chart_panel_render() call. Must be called between BeginDrawing()
 *        and EndDrawing(), after the Clay render-command pass.
 */
void chart_panel_draw(AppState* state);

/**
 * @brief Push a sample into every chart series whose @p topic matches.
 *        Walks the JSON payload via cJSON to extract each series's dot path.
 *        Called from main.c's drain loop just before the message payload
 *        is freed.
 */
void chart_panel_capture_sample(AppState* state, const char* topic, const uint8_t* payload, uint32_t payload_len,
                                uint64_t ts_us);

#endif
