// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef ARENA_ALLOC_H
#define ARENA_ALLOC_H

#include <stddef.h>
#include <stdint.h>

typedef struct ArenaChunk {
    uint8_t* memory;
    size_t size;
    size_t cursor;
    struct ArenaChunk* next;
} ArenaChunk;

typedef struct {
    size_t chunk_size; // default chunk size for new allocations
    ArenaChunk* current; // active chunk
    ArenaChunk* chunks; // head of linked list of all chunks
} ArenaAlloc;

/**
 * @brief Initialize a bump arena allocator.
 * @param arena       Arena to initialize (caller-allocated).
 * @param chunk_size  Default byte size for each heap chunk.
 */
void arena_alloc_init(ArenaAlloc* arena, size_t chunk_size);

/** @brief Free all chunks and reset all fields. */
void arena_alloc_destroy(ArenaAlloc* arena);

/**
 * @brief Bump-allocate @p size bytes with natural alignment.
 *
 * Allocates a new chunk if the current one is full.
 * @return Pointer into arena storage, or NULL on OOM.
 */
void* arena_alloc_get(ArenaAlloc* arena, size_t size);

/**
 * @brief Reset all chunk cursors to zero (keeps heap allocations).
 *
 * All pointers previously returned by arena_alloc_get() are invalidated.
 */
void arena_alloc_reset(ArenaAlloc* arena);

/**
 * @brief Total bytes committed across all chunks, including alignment padding.
 * @return Value ≥ the sum of all requested allocation sizes.
 */
size_t arena_alloc_used(const ArenaAlloc* arena);

#endif
