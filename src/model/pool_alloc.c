// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "model/alloc.h"
#include "model/pool_alloc.h"

static PoolSlab* slab_new(size_t elem_size, uint32_t capacity) {
    PoolSlab* slab = alloc_check(malloc(sizeof(PoolSlab)));
    slab->memory = alloc_check(calloc(capacity, elem_size));
    slab->next = NULL;
    return slab;
}

void pool_alloc_init(PoolAlloc* pool, size_t elem_size, uint32_t slab_capacity) {
    if (elem_size < sizeof(void*)) {
        elem_size = sizeof(void*);
    }
    pool->elem_size = elem_size;
    pool->slab_capacity = slab_capacity;
    pool->used_count = 0;
    pool->free_list = NULL;
    pool->slabs = slab_new(elem_size, slab_capacity);
    pool->slab_cursor = 0;
}

void pool_alloc_destroy(PoolAlloc* pool) {
    PoolSlab* slab = pool->slabs;
    while (slab) {
        PoolSlab* next = slab->next;
        free(slab->memory);
        free(slab);
        slab = next;
    }
    pool->slabs = NULL;
    pool->free_list = NULL;
    pool->used_count = 0;
}

void* pool_alloc_get(PoolAlloc* pool) {
    pool->used_count++;

    if (pool->free_list) {
        void* ptr = pool->free_list;
        void* next;
        memcpy(&next, ptr, sizeof(void*));
        pool->free_list = next;
        memset(ptr, 0, pool->elem_size);
        return ptr;
    }

    if (pool->slab_cursor >= pool->slab_capacity) {
        PoolSlab* new_slab = slab_new(pool->elem_size, pool->slab_capacity);
        new_slab->next = pool->slabs;
        pool->slabs = new_slab;
        pool->slab_cursor = 0;
    }

    void* ptr = pool->slabs->memory + (size_t)pool->slab_cursor * pool->elem_size;
    pool->slab_cursor++;
    return ptr;
}

void pool_alloc_put(PoolAlloc* pool, void* ptr) {
    assert(pool->used_count > 0);
    memcpy(ptr, &pool->free_list, sizeof(void*));
    pool->free_list = ptr;
    pool->used_count--;
}

uint32_t pool_alloc_used(const PoolAlloc* pool) {
    return pool->used_count;
}
