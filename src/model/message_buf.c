// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "model/alloc.h"
#include "model/message_buf.h"

void message_buf_init(MessageBuf* buf, uint32_t capacity) {
    buf->entries = alloc_check(calloc(capacity, sizeof(MessageRecord)));
    buf->capacity = capacity;
    buf->head = 0;
    buf->count = 0;
}

void message_buf_destroy(MessageBuf* buf) {
    free(buf->entries);
    buf->entries = NULL;
    buf->count = 0;
}

void message_buf_push(MessageBuf* buf, const MessageRecord* record) {
    uint32_t idx = (buf->head + buf->count) % buf->capacity;
    if (buf->count == buf->capacity) {
        buf->head = (buf->head + 1) % buf->capacity;
    } else {
        buf->count++;
    }
    buf->entries[idx] = *record;
}

const MessageRecord* message_buf_get(const MessageBuf* buf, uint32_t index) {
    if (index >= buf->count) {
        return NULL;
    }
    uint32_t real_idx = (buf->head + index) % buf->capacity;
    return &buf->entries[real_idx];
}

uint32_t message_buf_count(const MessageBuf* buf) {
    return buf->count;
}

void message_buf_clear(MessageBuf* buf) {
    buf->head = 0;
    buf->count = 0;
}
