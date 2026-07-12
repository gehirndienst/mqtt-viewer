// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef UI_H
#define UI_H

#include <stdbool.h>

#include "raylib.h"

/**
 * @brief Context returned by ui_init(). Pass to ui_shutdown() on exit.
 *
 * Holds the Clay arena heap allocation that must outlive the render loop.
 */
typedef struct {
    void* clay_mem; /**< Heap block for the Clay arena; freed by ui_shutdown() */
} UiCtx;

/**
 * @brief Initialize the Raylib window (1200×800, resizable), load embedded
 *        fonts, and set up the Clay layout engine.
 *
 * Must be called before any Raylib or Clay function. On failure the window
 * is not opened and @p ctx is left uninitialized.
 * @param ctx  Output; pass to ui_shutdown() when the application exits.
 * @return true on success; false if Clay arena malloc fails.
 */
bool ui_init(UiCtx* ctx);

/**
 * @brief Unload fonts, free the Clay arena, and close the Raylib window.
 * @param ctx  Context returned by ui_init().
 */
void ui_shutdown(UiCtx* ctx);

/**
 * @brief Return the Raylib Font for a given font ID.
 * @param font_id  FONT_DEFAULT (0) or FONT_MONO (1), defined in ui/theme.h.
 */
Font ui_get_font(int font_id);

#endif
