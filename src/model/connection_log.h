// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef CONNECTION_LOG_H
#define CONNECTION_LOG_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CONN_LOG_INFO,
    CONN_LOG_WARN,
    CONN_LOG_ERROR,
} LogLevel;

typedef struct {
    uint64_t timestamp_us;
    LogLevel level;
    char message[256];
} LogEntry;

static_assert(sizeof(((LogEntry*)0)->message) >= 128,
              "LogEntry.message too small; downstream code requires >= 128 bytes");

typedef struct {
    LogEntry* entries;
    uint32_t capacity;
    uint32_t head;
    uint32_t count;
    pthread_mutex_t mutex;
} ConnectionLog;

/**
 * @brief Initialize a thread-safe ring buffer for log entries.
 *
 * A mutex protects all operations; log entries may be added from any thread.
 * @param log       Log to initialize (caller-allocated).
 * @param capacity  Maximum entries before oldest are overwritten.
 */
void connection_log_init(ConnectionLog* log, uint32_t capacity);

/** @brief Destroy the log and release all resources. */
void connection_log_destroy(ConnectionLog* log);

/**
 * @brief Append a log entry (thread-safe).
 * @param log      Log handle.
 * @param level    Severity: CONN_LOG_INFO, CONN_LOG_WARN, or CONN_LOG_ERROR.
 * @param message  NUL-terminated message string (truncated to 255 chars).
 */
void connection_log_add(ConnectionLog* log, LogLevel level, const char* message);

/**
 * @brief Copy the entry at logical @p index into @p out (thread-safe, by value).
 *
 * Returns false - and leaves @p out unmodified - when @p index ≥ count.
 * Using by-value copy avoids a data race: the ring buffer entry may be
 * overwritten between get() and the caller's read of the returned fields.
 *
 * @param log    Log handle.
 * @param index  Logical index (0 = oldest remaining entry).
 * @param out    Destination to copy the entry into.
 * @return true on success; false if @p index is out of range.
 */
bool connection_log_get(ConnectionLog* log, uint32_t index, LogEntry* out);

/**
 * @brief Non-atomic snapshot of the current entry count (thread-safe).
 * @return Number of entries currently in the log (≤ capacity).
 */
uint32_t connection_log_count(ConnectionLog* log);

/** @brief Remove all entries from the log (thread-safe). */
void connection_log_clear(ConnectionLog* log);

#endif
