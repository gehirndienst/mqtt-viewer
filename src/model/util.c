// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <string.h>
#include <time.h>

#include "model/util.h"

uint64_t util_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

void util_str_copy(char* dst, size_t cap, const char* src) {
    if (cap == 0) return;
    if (!src) src = "";
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void util_fmt_hhmmss(uint64_t ts_us, char* out, size_t cap) {
    time_t secs = (time_t)(ts_us / 1000000ULL);
    struct tm tm_info;
    if (!localtime_r(&secs, &tm_info) || strftime(out, cap, "%H:%M:%S", &tm_info) == 0) {
        util_str_copy(out, cap, "--:--:--");
    }
}

size_t util_preview_sanitize(char* dst, size_t cap, const uint8_t* src, size_t len) {
    if (cap == 0) return 0;
    size_t n = len < cap - 1 ? len : cap - 1;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = src[i];
        dst[i] = (c < 0x20 || c == 0x7f) ? ' ' : (char)c;
    }
    dst[n] = '\0';
    return n;
}

void util_preview_build_compact(char* dst, size_t cap, const uint8_t* src, size_t len) {
    if (cap < 5) {
        if (cap) dst[0] = '\0';
        return;
    }
    // 4 bytes reserved for the optional "..." terminator
    size_t room = cap - 4;
    bool truncated = len > room;
    size_t n = util_preview_sanitize(dst, room + 1, src, truncated ? room : len);

    size_t s = 0, d = 0;
    while (s < n) {
        if (dst[s] == ' ') {
            if (d > 0 && dst[d - 1] != ' ') dst[d++] = ' ';
            s++;
        } else {
            dst[d++] = dst[s++];
        }
    }
    if (d > 0 && dst[d - 1] == ' ') d--;

    if (truncated) {
        memcpy(dst + d, "...", 4);
    } else {
        dst[d] = '\0';
    }
}
