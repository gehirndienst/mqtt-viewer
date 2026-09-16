// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
/**
 * @file
 * @brief Abort-on-OOM allocation wrapper
 */
#ifndef ALLOC_H
#define ALLOC_H

#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Return @p ptr, or abort the process if it is NULL
 * @param ptr  Pointer to check
 * @return @p ptr if not NULL
 */
static inline void* alloc_check(void* ptr) {
    if (ptr == NULL) {
        fprintf(stderr, "mqtt-viewer: OOM\n");
        abort();
    }
    return ptr;
}

#endif
