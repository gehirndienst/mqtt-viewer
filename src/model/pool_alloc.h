// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef POOL_ALLOC_H
#define POOL_ALLOC_H

#include <stddef.h>
#include <stdint.h>

typedef struct PoolSlab {
    uint8_t* memory;
    struct PoolSlab* next;
} PoolSlab;

typedef struct {
    size_t elem_size; // size of each element (at least sizeof(void*))
    uint32_t slab_capacity; // elements per slab
    uint32_t used_count;
    void* free_list; // singly-linked list of freed slots
    PoolSlab* slabs; // linked list of allocated slabs
    uint32_t slab_cursor; // next free index in current slab
} PoolAlloc;

/**
 * @brief Initialize a slab pool allocator for fixed-size elements
 * @param pool           Pool to initialize (caller-allocated).
 * @param elem_size      Size of each element in bytes (≥ sizeof(void*)).
 * @param slab_capacity  Number of elements per slab.
 */
void pool_alloc_init(PoolAlloc* pool, size_t elem_size, uint32_t slab_capacity);

/** @brief Free all slabs and reset all fields. */
void pool_alloc_destroy(PoolAlloc* pool);

/**
 * @brief Allocate one element from the pool (O(1) amortised).
 *
 * Grows by one slab when the free list and current slab are exhausted.
 * @return Pointer to the allocated element, or NULL on OOM.
 */
void* pool_alloc_get(PoolAlloc* pool);

/**
 * @brief Return @p ptr to the pool's free list. @p ptr must have been
 *        obtained from the same pool via pool_alloc_get().
 */
void pool_alloc_put(PoolAlloc* pool, void* ptr);

/** @brief Number of elements currently allocated (not on the free list). */
uint32_t pool_alloc_used(const PoolAlloc* pool);

#endif
