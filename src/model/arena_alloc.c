// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "model/alloc.h"
#include "model/arena_alloc.h"

#define ARENA_ALIGNMENT 8

static size_t align_up(size_t n, size_t align) {
    return (n + align - 1) & ~(align - 1);
}

static ArenaChunk* chunk_new(size_t size) {
    ArenaChunk* chunk = alloc_check(malloc(sizeof(ArenaChunk)));
    chunk->memory = alloc_check(malloc(size));
    chunk->size = size;
    chunk->cursor = 0;
    chunk->next = NULL;
    return chunk;
}

void arena_alloc_init(ArenaAlloc* arena, size_t chunk_size) {
    arena->chunk_size = chunk_size;
    arena->chunks = chunk_new(chunk_size);
    arena->current = arena->chunks;
}

void arena_alloc_destroy(ArenaAlloc* arena) {
    ArenaChunk* chunk = arena->chunks;
    while (chunk) {
        ArenaChunk* next = chunk->next;
        free(chunk->memory);
        free(chunk);
        chunk = next;
    }
    arena->chunks = NULL;
    arena->current = NULL;
}

void* arena_alloc_get(ArenaAlloc* arena, size_t size) {
    size_t aligned_size = align_up(size, ARENA_ALIGNMENT);
    ArenaChunk* c = arena->current;

    if (c->cursor + aligned_size > c->size) {
        size_t new_size = arena->chunk_size;
        if (aligned_size > new_size) {
            new_size = aligned_size;
        }
        ArenaChunk* fresh = chunk_new(new_size);
        fresh->next = arena->chunks;
        arena->chunks = fresh;
        arena->current = fresh;
        c = fresh;
    }

    void* ptr = c->memory + c->cursor;
    c->cursor += aligned_size;
    // zero requested bytes only; padding bytes are internal
    memset(ptr, 0, size);
    return ptr;
}

void arena_alloc_reset(ArenaAlloc* arena) {
    // keep the largest chunk ,free all others
    ArenaChunk* keep = NULL;
    ArenaChunk* chunk = arena->chunks;
    while (chunk) {
        ArenaChunk* next = chunk->next;
        if (keep == NULL || chunk->size > keep->size) {
            if (keep) {
                free(keep->memory);
                free(keep);
            }
            keep = chunk;
        } else {
            free(chunk->memory);
            free(chunk);
        }
        chunk = next;
    }
    assert(keep != NULL);
    keep->cursor = 0;
    keep->next = NULL;
    arena->chunks = keep;
    arena->current = keep;
}

size_t arena_alloc_used(const ArenaAlloc* arena) {
    size_t total = 0;
    for (ArenaChunk* c = arena->chunks; c; c = c->next) {
        total += c->cursor;
    }
    return total;
}
