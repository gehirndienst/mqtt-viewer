// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "model/alloc.h"
#include "model/spsc_queue.h"

void spsc_queue_init(SpscQueue* q, uint32_t capacity, uint32_t elem_size) {
    q->capacity = capacity;
    q->elem_size = elem_size;
    q->buffer = alloc_check(malloc(((size_t)capacity + 1) * elem_size));
    atomic_store_explicit(&q->head, 0, memory_order_relaxed);
    atomic_store_explicit(&q->tail, 0, memory_order_relaxed);
}

void spsc_queue_destroy(SpscQueue* q) {
    free(q->buffer);
    q->buffer = NULL;
}

bool spsc_queue_push(SpscQueue* q, const void* elem) {
    uint32_t tail = atomic_load_explicit(&q->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&q->head, memory_order_acquire);
    uint32_t next_tail = (tail + 1) % (q->capacity + 1);
    if (next_tail == head) {
        return false;
    }
    memcpy(q->buffer + (size_t)tail * q->elem_size, elem, q->elem_size);
    atomic_store_explicit(&q->tail, next_tail, memory_order_release);
    return true;
}

bool spsc_queue_pop(SpscQueue* q, void* elem) {
    uint32_t head = atomic_load_explicit(&q->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&q->tail, memory_order_acquire);
    if (head == tail) {
        return false;
    }
    memcpy(elem, q->buffer + (size_t)head * q->elem_size, q->elem_size);
    uint32_t next_head = (head + 1) % (q->capacity + 1);
    atomic_store_explicit(&q->head, next_head, memory_order_release);
    return true;
}

uint32_t spsc_queue_drain(SpscQueue* q, void* buf, uint32_t max_count) {
    uint32_t count = 0;
    uint8_t* out = buf;
    while (count < max_count && spsc_queue_pop(q, out + (size_t)count * q->elem_size)) {
        count++;
    }
    return count;
}

uint32_t spsc_queue_count(const SpscQueue* q) {
    uint32_t head = atomic_load_explicit(&q->head, memory_order_acquire);
    uint32_t tail = atomic_load_explicit(&q->tail, memory_order_acquire);
    if (tail >= head) {
        return tail - head;
    }
    return (q->capacity + 1) - head + tail;
}

bool spsc_queue_empty(const SpscQueue* q) {
    return spsc_queue_count(q) == 0;
}
