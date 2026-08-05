// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "clay.h"
#include "raylib.h"

#include "platform/ui.h"
#include "ui/theme.h"

static Font s_fonts[2];

static const unsigned char s_font_default_data[] = {
#embed "../../resources/fonts/Inter-Regular.ttf"
};
static const unsigned char s_font_mono_data[] = {
#embed "../../resources/fonts/JetBrainsMono-Regular.ttf"
};

static Clay_Dimensions measure_text(Clay_StringSlice text, Clay_TextElementConfig* config, void* userData) {
    (void)userData;
    Font font = s_fonts[config->fontId];
    float spacing = 1.0f;
    static char buf[4096];
    uint32_t len = (uint32_t)text.length < sizeof(buf) - 1 ? (uint32_t)text.length : (uint32_t)(sizeof(buf) - 1);
    memcpy(buf, text.chars, len);
    buf[len] = '\0';
    Vector2 size = MeasureTextEx(font, buf, (float)config->fontSize, spacing);
    return (Clay_Dimensions){.width = size.x, .height = size.y};
}

static void clay_error_handler(Clay_ErrorData error) {
    TraceLog(LOG_WARNING, "Clay error: %.*s", (int)error.errorText.length, error.errorText.chars);
}

static int s_codepoints[256];
static int s_codepoint_count = 0;

static void build_codepoints(void) {
    s_codepoint_count = 0;

    for (int i = 32; i <= 255; i++) s_codepoints[s_codepoint_count++] = i;

    // additional Unicode symbols used in the UI
    static const int extras[] = {
        0x03A3, // Σ GREEK CAPITAL SIGMA - subtree message-count badge
        0x2022, // • BULLET - password mask
        0x25B2, // ▲ UP TRIANGLE - Publish button label
        0x25B8, // ▸ SMALL RIGHT TRI - tree node collapsed
        0x25BE, // ▾ SMALL DOWN TRI - tree node expanded
        0x25CB, // ○ WHITE CIRCLE - toggle disabled
        0x25CF, // ● BLACK CIRCLE - connection indicator
        0x2713, // ✓ CHECK MARK - toggle enabled
    };
    for (int i = 0; i < (int)(sizeof(extras) / sizeof(extras[0])); i++) s_codepoints[s_codepoint_count++] = extras[i];
}

bool ui_init(UiCtx* ctx) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1280, 800, "MQTT Viewer");
    SetExitKey(KEY_NULL);
    SetWindowMinSize(960, 640);

    build_codepoints();
    s_fonts[FONT_DEFAULT] = LoadFontFromMemory(".ttf", s_font_default_data, (int)sizeof(s_font_default_data), 40,
                                               s_codepoints, s_codepoint_count);
    s_fonts[FONT_MONO] = LoadFontFromMemory(".ttf", s_font_mono_data, (int)sizeof(s_font_mono_data), 40, s_codepoints,
                                            s_codepoint_count);
    SetTextureFilter(s_fonts[FONT_DEFAULT].texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(s_fonts[FONT_MONO].texture, TEXTURE_FILTER_BILINEAR);

    Clay_SetMaxElementCount(32768);
    uint64_t clay_mem_size = Clay_MinMemorySize();
    ctx->clay_mem = malloc(clay_mem_size);
    if (!ctx->clay_mem) return false;
    Clay_Arena clay_arena = Clay_CreateArenaWithCapacityAndMemory(clay_mem_size, ctx->clay_mem);
    Clay_Initialize(clay_arena, (Clay_Dimensions){.width = (float)GetScreenWidth(), .height = (float)GetScreenHeight()},
                    (Clay_ErrorHandler){.errorHandlerFunction = clay_error_handler});
    Clay_SetMeasureTextFunction(measure_text, NULL);

    return true;
}

void ui_shutdown(UiCtx* ctx) {
    UnloadFont(s_fonts[FONT_DEFAULT]);
    UnloadFont(s_fonts[FONT_MONO]);
    free(ctx->clay_mem);
    ctx->clay_mem = NULL;
    CloseWindow();
}

Font ui_get_font(int font_id) {
    return s_fonts[font_id];
}
