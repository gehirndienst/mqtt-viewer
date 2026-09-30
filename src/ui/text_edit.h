// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
/**
 * @file
 * @brief Caret-aware text editing shared by the inline text fields
 */
#ifndef MV_UI_TEXT_EDIT_H
#define MV_UI_TEXT_EDIT_H

#include <stdbool.h>
#include <stddef.h>

#define TEXT_EDIT_NO_MARKER ((size_t)-1)

/**
 * @brief Apply this frame's keyboard input to a field buffer
 *
 * Handles typed characters, paste, Backspace/Delete, Left/Right/Home/End (Ctrl/Cmd+arrows jump to the ends), and the
 * select-all / copy shortcuts. Typing or pasting over a select-all replaces the whole text. Escape and Tab are left to
 * the caller
 *
 * @param buf Field buffer holding a NUL-terminated string.
 * @param cap Total capacity of @p buf in bytes.
 * @param caret Caret byte offset into @p buf; clamped and updated in place.
 * @param all_selected Select-all flag; set by Ctrl/Cmd+A and cleared by any edit or caret move.
 * @param allow_newlines Keep line breaks when pasting (multi-line fields).
 * @param ascii_only Ignore typed characters outside printable ASCII.
 * @return true if the buffer content changed.
 */
bool text_edit_update(char* buf, size_t cap, size_t* caret, bool* all_selected, bool allow_newlines, bool ascii_only);

/**
 * @brief Format @p raw for display with a '|' caret marker inserted at @p caret
 *
 * Text longer than fits in @p out_cap is cut and suffixed with "..."; the marker is clamped to the visible part
 *
 * @param out Destination buffer.
 * @param out_cap Capacity of @p out in bytes.
 * @param raw Field text.
 * @param caret Caret byte offset into @p raw.
 * @param show_marker false renders the plain text without a marker.
 * @return Byte offset of the marker in @p out, or TEXT_EDIT_NO_MARKER when none was inserted.
 */
size_t text_edit_format(char* out, size_t out_cap, const char* raw, size_t caret, bool show_marker);

/**
 * @brief Map a click inside a rendered field to a caret offset in the raw text
 *
 * Measures the displayed text with the same font and spacing as the Clay measure callback, so the answer matches what
 * is on screen. Wrapped text is broken at spaces like Clay does
 *
 * @param display String as rendered (from text_edit_format()).
 * @param marker Marker offset returned by text_edit_format().
 * @param font_id FONT_DEFAULT or FONT_MONO.
 * @param font_size Font size in Clay units.
 * @param wrap_width Content width to wrap at; 0 disables wrapping (explicit '\n' still breaks lines).
 * @param rel_x Click x relative to the text origin.
 * @param rel_y Click y relative to the text origin.
 * @return Caret byte offset into the raw (marker-free) text.
 */
size_t text_edit_hit_test(const char* display, size_t marker, int font_id, float font_size, float wrap_width,
                          float rel_x, float rel_y);

#endif
