// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef MV_UI_UTIL_H
#define MV_UI_UTIL_H

#include <stdbool.h>

#include "clay.h"

/** @brief How long "Copied"-style confirmations stay on screen, in seconds */
#define UI_UTILS_FLASH_SECS 1.5f

/**
 * @brief Wrap a NUL-terminated C string as a Clay_String (no copy).
 *
 * The pointer must outlive the current Clay layout pass - use static or arena storage for text, never a
 * stack buffer that goes out of scope before Clay_EndLayout().
 */
Clay_String ui_utils_clay_string(const char* s);

/** @brief True when the point (@p x, @p y) lies inside @p b (inclusive edges). */
bool ui_utils_bbox_contains(Clay_BoundingBox b, float x, float y);

/** @brief Start (or restart) a confirmation flash. */
void ui_utils_flash_start(float* timer);

/**
 * @brief Advance a confirmation flash by this frame's time.
 * @return true while the flash is still showing.
 */
bool ui_utils_flash_tick(float* timer);

#endif
