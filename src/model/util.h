// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef MV_UTIL_H
#define MV_UTIL_H

#include <stddef.h>
#include <stdint.h>

/** @brief Wall-clock time in microseconds (CLOCK_REALTIME). Message timestamps and UI ages share this clock. */
uint64_t util_now_us(void);

/** @brief Format a microsecond wall-clock timestamp as local "HH:MM:SS" into @p out (cap >= 9). */
void util_fmt_hhmmss(uint64_t ts_us, char* out, size_t cap);

/**
 * @brief Bounded string copy: at most @p cap - 1 bytes, always NUL-terminated.
 * @param dst  Destination buffer (cap >= 1).
 * @param cap  Total size of @p dst in bytes.
 * @param src  Source string; NULL is treated as "".
 */
void util_str_copy(char* dst, size_t cap, const char* src);

/**
 * @brief Copy a payload excerpt into @p dst with control bytes (< 0x20, 0x7f) replaced by spaces.
 *
 * Copies min(len, cap - 1) bytes and NUL-terminates.
 * @return Number of bytes written, excluding the terminator.
 */
size_t util_preview_sanitize(char* dst, size_t cap, const uint8_t* src, size_t len);

/**
 * @brief Build the topic tree's one-line payload preview: sanitised, runs of whitespace collapsed to a single
 *        space, leading/trailing space dropped, and "..." appended when the payload did not fit.
 * @param cap  Total size of @p dst; must be >= 5 to leave room for the ellipsis.
 */
void util_preview_build_compact(char* dst, size_t cap, const uint8_t* src, size_t len);

#endif
