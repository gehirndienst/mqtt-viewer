// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef DB_H
#define DB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "model/broker_profile.h"
#include "model/message_buf.h"

typedef struct Db Db;

/**
 * @brief Open (or create) the SQLite database at @p db_path.
 * @return New Db handle, or NULL on failure.
 */
Db* db_open(const char* db_path);

/** @brief Flush pending writes and close the database. */
void db_close(Db* db);

/**
 * @brief Insert or update a broker profile row.
 * @param profile  Profile to persist. If profile->id == -1, a new row is
 *                 created and profile->id is updated on success.
 * @return true on success.
 */
bool db_save_profile(Db* db, BrokerProfile* profile);

/**
 * @brief Delete the profile row with the given @p profile_id.
 * @return true on success; false if the row did not exist or a DB error occurred.
 */
bool db_delete_profile(Db* db, int profile_id);

/**
 * @brief Load up to @p max_count profiles into @p profiles.
 * @return Number of profiles loaded, or -1 on error.
 */
int db_load_profiles(Db* db, BrokerProfile* profiles, int max_count);

/**
 * @brief Upsert a key/value setting row.
 * @param key    NUL-terminated setting name.
 * @param value  NUL-terminated setting value.
 * @return true on success.
 */
bool db_set_setting(Db* db, const char* key, const char* value);

/**
 * @brief Read a setting value by key.
 * @param key       NUL-terminated setting name.
 * @param fallback  Returned when the key does not exist (may be NULL).
 * @return Pointer to an internal buffer valid until the next db_get_setting()
 *         call, or @p fallback when the key is absent.
 */
const char* db_get_setting(Db* db, const char* key, const char* fallback);

/**
 * @brief Persist an array of message records. The raw payload bytes are
 *        stored in the payload BLOB column (NULL when payload is NULL/empty).
 * @param records  Array of records to insert.
 * @param count    Number of records in @p records.
 * @return true on success.
 */
bool db_save_messages(Db* db, const MessageRecord* records, int count);

/**
 * @brief Load up to @p max_count message records (newest first).
 *
 * Each returned record's `payload` is heap-allocated (or NULL if the stored
 * payload was NULL/empty); the caller owns it and must free() it.
 *
 * @return Number of records loaded, or -1 on error.
 */
int db_load_messages(Db* db, MessageRecord* records, int max_count);

/**
 * @brief Delete oldest rows so that no more than @p max_rows remain.
 * @return true on success.
 */
bool db_trim_messages(Db* db, int max_rows);

/**
 * @brief Persist message records not yet written from the history ring, then trim the table.
 * @param pushed Monotonic number of records ever pushed into @p history.
 * @param saved  Monotonic number of records already persisted; advanced on return.
 */
void db_flush_history(Db* db, MessageBuf* history, uint64_t pushed, uint64_t* saved);

/**
 * @brief Full-text search over all stored messages (topic + payload), ranked by relevance.
 *
 * @p query's words must all appear somewhere in the topic or payload (any order), with the
 * last word treated as a live prefix (so "conn" matches "connection" while typing). Special
 * FTS5 syntax characters in @p query are treated as literal text, not query operators.
 *
 * Returned records never have `payload` populated (always NULL/0) - only `preview` is
 * filled in, so callers don't need to free() anything.
 *
 * @return Number of records found, or -1 on error.
 */
int db_search_messages(Db* db, const char* query, MessageRecord* results, int max_count);

/**
 * @brief UTIL: Resolve the platform-appropriate database file path into @p out.
 *
 * Priority: $MQTT_VIEWER_DB_PATH - macOS Application Support / XDG data dir.
 * Creates the parent directory if it does not exist.
 * @param out       Destination buffer.
 * @param out_size  Size of @p out in bytes.
 */
void db_resolve_path(char* out, size_t out_size);

#endif
