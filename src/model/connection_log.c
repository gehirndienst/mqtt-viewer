// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "model/alloc.h"
#include "model/connection_log.h"
#include "model/util.h"

void connection_log_init(ConnectionLog* log, uint32_t capacity) {
    log->entries = alloc_check(calloc(capacity, sizeof(LogEntry)));
    log->capacity = capacity;
    log->head = 0;
    log->count = 0;
    pthread_mutex_init(&log->mutex, NULL);
}

void connection_log_destroy(ConnectionLog* log) {
    pthread_mutex_destroy(&log->mutex);
    free(log->entries);
    log->entries = NULL;
    log->count = 0;
}

void connection_log_add(ConnectionLog* log, LogLevel level, const char* message) {
    pthread_mutex_lock(&log->mutex);
    uint32_t idx = (log->head + log->count) % log->capacity;
    if (log->count == log->capacity) {
        log->head = (log->head + 1) % log->capacity;
    } else {
        log->count++;
    }
    log->entries[idx].timestamp_us = util_now_us();
    log->entries[idx].level = level;
    util_str_copy(log->entries[idx].message, sizeof(log->entries[idx].message), message);
    pthread_mutex_unlock(&log->mutex);
}

bool connection_log_get(ConnectionLog* log, uint32_t index, LogEntry* out) {
    pthread_mutex_lock(&log->mutex);
    if (index >= log->count) {
        pthread_mutex_unlock(&log->mutex);
        return false;
    }
    uint32_t real_idx = (log->head + index) % log->capacity;
    *out = log->entries[real_idx];
    pthread_mutex_unlock(&log->mutex);
    return true;
}

uint32_t connection_log_count(ConnectionLog* log) {
    pthread_mutex_lock(&log->mutex);
    uint32_t count = log->count;
    pthread_mutex_unlock((pthread_mutex_t*)&log->mutex);
    return count;
}

void connection_log_clear(ConnectionLog* log) {
    pthread_mutex_lock(&log->mutex);
    log->head = 0;
    log->count = 0;
    pthread_mutex_unlock(&log->mutex);
}
