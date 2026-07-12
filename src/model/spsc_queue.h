// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef SPSC_QUEUE_H
#define SPSC_QUEUE_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t* buffer;
    uint32_t capacity; // max number of elements
    uint32_t elem_size; // size of each element in bytes
    alignas(64) atomic_uint_least32_t head; // written by consumer
    alignas(64) atomic_uint_least32_t tail; // written by producer
} SpscQueue;

static_assert(alignof(SpscQueue) >= 8, "SpscQueue alignment");

/**
 * @brief Initialize an SPSC queue backed by a heap-allocated ring buffer.
 * @param q          Queue to initialize (caller-allocated).
 * @param capacity   Maximum number of elements the queue can hold.
 * @param elem_size  Size of each element in bytes.
 */
void spsc_queue_init(SpscQueue* q, uint32_t capacity, uint32_t elem_size);

/** @brief Free the ring buffer and reset all fields. */
void spsc_queue_destroy(SpscQueue* q);

/**
 * @brief Push one element onto the queue (producer thread only).
 * @param q     Queue handle.
 * @param elem  Pointer to the element to copy in.
 * @return true on success; false if the queue is full (elem is not consumed).
 */
bool spsc_queue_push(SpscQueue* q, const void* elem);

/**
 * @brief Pop one element from the queue (consumer thread only).
 * @param q     Queue handle.
 * @param elem  Destination buffer (must be at least elem_size bytes).
 * @return true on success; false if the queue is empty.
 */
bool spsc_queue_pop(SpscQueue* q, void* elem);

/**
 * @brief Drain up to @p max_count elements into a contiguous buffer.
 * @param q          Queue handle (consumer thread only).
 * @param buf        Destination buffer (must hold max_count × elem_size bytes).
 * @param max_count  Maximum elements to consume.
 * @return Number of elements actually drained (0 if empty).
 */
uint32_t spsc_queue_drain(SpscQueue* q, void* buf, uint32_t max_count);

/**
 * @brief Non-atomic approximate element count. Safe for monitoring only.
 *
 * Do not use the return value to make push/pop decisions from the
 * non-owning thread - it may be stale by the time it is read.
 */
uint32_t spsc_queue_count(const SpscQueue* q);

/** @brief Return true when the queue contains no elements. */
bool spsc_queue_empty(const SpscQueue* q);

#endif
