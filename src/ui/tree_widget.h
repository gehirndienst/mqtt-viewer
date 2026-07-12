// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef TREE_WIDGET_H
#define TREE_WIDGET_H

#include "model/app_state.h"

/**
 * @brief Render the topic tree widget into the current Clay layout.
 *
 * Handles keyboard navigation and selection. Must be called inside an active
 * Clay layout pass (between Clay_BeginLayout and Clay_EndLayout).
 */
void tree_widget_render(AppState* state);

#endif
