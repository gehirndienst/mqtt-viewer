// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef ALLOC_H
#define ALLOC_H

#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Return @p ptr, or abort the process if it is NULL.
 *
 * Wrap malloc()/calloc() with this so an out-of-memory result fails loudly
 * with a diagnostic instead of silently dereferencing NULL. Unlike assert(),
 * the check stays active in release (NDEBUG) builds.
 */
static inline void* alloc_check(void* ptr) {
    if (ptr == NULL) {
        fprintf(stderr, "mqtt-viewer: OOM\n");
        abort();
    }
    return ptr;
}

#endif
