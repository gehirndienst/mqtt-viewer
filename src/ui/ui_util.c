// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <string.h>

#include "raylib.h"

#include "ui/ui_util.h"

Clay_String ui_utils_clay_string(const char* s) {
    return (Clay_String){.length = (int32_t)strlen(s), .chars = s};
}

bool ui_utils_bbox_contains(Clay_BoundingBox b, float x, float y) {
    return x >= b.x && x <= b.x + b.width && y >= b.y && y <= b.y + b.height;
}

void ui_utils_flash_start(float* timer) {
    *timer = UI_UTILS_FLASH_SECS;
}

bool ui_utils_flash_tick(float* timer) {
    if (*timer <= 0.0f) return false;
    *timer -= GetFrameTime();
    return *timer > 0.0f;
}
