// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef MESSAGE_BUF_H
#define MESSAGE_BUF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MSG_PREVIEW_LEN 512

typedef struct {
    uint64_t timestamp_us;
    char topic[256]; // full topic path, NUL-terminated owned copy
    uint8_t* payload; // NOT owned; caller frees after push
    uint32_t payload_len;
    uint8_t qos;
    bool retained;
    bool dup;
    uint32_t broker_id;
    char preview[MSG_PREVIEW_LEN]; // sanitized payload excerpt
} MessageRecord;

typedef struct {
    MessageRecord* entries;
    uint32_t capacity;
    uint32_t head; // index of oldest entry
    uint32_t count;
} MessageBuf;

/**
 * @brief Initialize a fixed-capacity ring buffer for MessageRecord values.
 * @param buf       Buffer to initialize (caller-allocated).
 * @param capacity  Maximum number of records before oldest are evicted.
 */
void message_buf_init(MessageBuf* buf, uint32_t capacity);

/** @brief Free the entries array and reset all fields. */
void message_buf_destroy(MessageBuf* buf);

/**
 * @brief Append a record, evicting the oldest if the buffer is full.
 * @param buf     Buffer handle.
 * @param record  Record to copy in. @p record->payload is not owned by
 *                the buffer; the caller remains responsible for it.
 */
void message_buf_push(MessageBuf* buf, const MessageRecord* record);

/**
 * @brief Access record at logical index @p index (0 = oldest).
 * @return Pointer into buffer storage, or NULL if @p index ≥ count.
 */
const MessageRecord* message_buf_get(const MessageBuf* buf, uint32_t index);

/** @brief Number of records currently held (≤ capacity). */
uint32_t message_buf_count(const MessageBuf* buf);

/** @brief Reset count to zero; does not free the entries array. */
void message_buf_clear(MessageBuf* buf);

#endif
