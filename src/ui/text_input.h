#ifndef MV_UI_TEXT_INPUT_H
#define MV_UI_TEXT_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "raylib.h"

/**
 * @brief Append @p src to the NUL-terminated string in @p buf, filtering control characters
 *
 * @param buf Destination buffer holding a NUL-terminated string.
 * @param cap Total capacity of @p buf in bytes.
 * @param src Source string to append; NULL is a no-op.
 * @param allow_newlines Keep line breaks (as '\n') instead of stripping them.
 */
static inline void text_input_append_filtered(char* buf, size_t cap, const char* src, bool allow_newlines) {
    if (!buf || cap == 0 || !src) return;
    size_t len = strlen(buf);
    for (const char* p = src; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        char out;
        if (c == '\r' || c == '\n') {
            if (!allow_newlines) continue;
            if (c == '\r' && p[1] == '\n') p++;
            out = '\n';
        } else if (c < 0x20 || c == 0x7f) {
            continue;
        } else {
            out = (char)c;
        }
        if (len + 1 >= cap) break;
        buf[len++] = out;
    }
    buf[len] = '\0';
}

/**
 * @brief Check whether the paste shortcut (Ctrl+V / Cmd+V) fired this frame, without consuming it
 */
static inline bool text_input_paste_pressed(void) {
    bool mod = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) || IsKeyDown(KEY_LEFT_SUPER) ||
        IsKeyDown(KEY_RIGHT_SUPER);
    return mod && IsKeyPressed(KEY_V);
}

/**
 * @brief Append the system clipboard to @p buf if the paste shortcut fired this frame
 *
 * Control characters are stripped via text_input_append_filtered()
 *
 * @param buf Destination NUL-terminated string buffer.
 * @param cap Total capacity of @p buf in bytes.
 * @param allow_newlines Keep line breaks (multi-line fields) instead of stripping them.
 * @return true if a paste was performed.
 */
static inline bool text_input_handle_paste(char* buf, size_t cap, bool allow_newlines) {
    if (!text_input_paste_pressed()) return false;
    text_input_append_filtered(buf, cap, GetClipboardText(), allow_newlines);
    return true;
}

/**
 * @brief Check whether the select-all shortcut (Ctrl+A / Cmd+A) fired this frame
 */
static inline bool text_input_select_all_pressed(void) {
    bool mod = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) || IsKeyDown(KEY_LEFT_SUPER) ||
        IsKeyDown(KEY_RIGHT_SUPER);
    return mod && IsKeyPressed(KEY_A);
}

/**
 * @brief Copy @p buf to the system clipboard if the copy shortcut (Ctrl+C / Cmd+C) fired this frame
 *
 * @return true if a copy was performed.
 */
static inline bool text_input_handle_copy(const char* buf) {
    bool mod = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) || IsKeyDown(KEY_LEFT_SUPER) ||
        IsKeyDown(KEY_RIGHT_SUPER);
    if (!mod || !IsKeyPressed(KEY_C)) return false;
    SetClipboardText(buf ? buf : "");
    return true;
}

#endif
