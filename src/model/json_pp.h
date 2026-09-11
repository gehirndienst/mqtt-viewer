// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef JSON_PP_H
#define JSON_PP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JSON_PP_MAX_LINES 2048
#define JSON_PP_MAX_DEPTH 64
#define JSON_PP_KEY_LEN 128
#define JSON_PP_VAL_LEN 272 // fits a quoted 256-byte topic with a few escapes
#define JSON_PP_PATH_LEN 128 // dot path of a leaf, "a.b.0.c"; must match CHART_DOT_PATH_LEN

typedef enum {
    JSON_PP_VAL_PUNCT, // "{", "}", "[", "]"
    JSON_PP_VAL_STRING, // quoted string, quotes kept
    JSON_PP_VAL_ATOM, // number, true/false/null, or anything else unquoted
} JsonPPValKind;

/** @brief One rendered line of pretty-printed JSON. Text is kept verbatim from the source */
typedef struct {
    int depth;
    char key[JSON_PP_KEY_LEN]; // object key without quotes; "" for array items and closing brackets
    char sep[4]; // ": " when a key is present, else ""
    char val[JSON_PP_VAL_LEN];
    char trail[4]; // "," when another sibling follows, else ""
    JsonPPValKind val_kind;
    char dot_path[JSON_PP_PATH_LEN]; // path of this line's value ("" = root)
    bool is_numeric; // chartable: a number, or a string that parses as one
} JsonPPLine;

/**
 * @brief Lenient JSON line formatter. Not a validator: it emits whatever structure it can read and stops at the
 *        first thing it can't, so truncated or slightly broken payloads still show their shape.
 *
 * All state lives here - the tokenizer cursor is only meaningful during json_pp_run().
 */
typedef struct {
    JsonPPLine lines[JSON_PP_MAX_LINES];
    int line_count;
    const char* cache_src;
    uint64_t cache_key;
    const char* src;
    int src_len;
    int pos;
    int depth;
    char path[JSON_PP_PATH_LEN];
    size_t path_len;
} JsonPP;

/**
 * @brief True when @p src is worth feeding to the formatter: starts an object, array or string, or is a bare
 *        number. Everything else should be shown as plain text.
 */
bool json_pp_looks_like_json(const char* src);

/** @brief Format @p src into pp->lines, replacing whatever was there. Drops the cache mark. */
void json_pp_run(JsonPP* pp, const char* src);

/** @brief Same for a byte range that is not NUL-terminated (raw MQTT payloads). */
void json_pp_run_len(JsonPP* pp, const char* src, size_t len);

/**
 * @brief Like json_pp_run(), but a no-op while (@p src, @p key) equal the previous run's.
 *
 * @p src must be stable storage (a node preview, a history slot, a frozen message) and @p key must change whenever
 * its contents do - a message timestamp works.
 * @return true if the lines were rebuilt.
 */
bool json_pp_run_cached(JsonPP* pp, const char* src, uint64_t key);

/** @brief Declare pp->lines to be the formatted form of (@p src, @p key) - after restoring them by hand. */
void json_pp_mark_cached(JsonPP* pp, const char* src, uint64_t key);

/** @brief First line whose dot_path equals @p dot_path ("" = the root value), or NULL. */
const JsonPPLine* json_pp_find(const JsonPP* pp, const char* dot_path);

/**
 * @brief Numeric value of a line: a number atom, or a string whose whole contents parse as one.
 * @return false for brackets, literals like true/null, and non-numeric strings.
 */
bool json_pp_line_number(const JsonPPLine* line, double* out);

/**
 * @brief Numeric value at @p dot_path, or - when that path is absent and the whole payload is a bare number or a
 *        numeric string - the payload itself. Lets a hand-published "500" land on a series charted via "speed".
 * @return false when neither yields a finite number.
 */
bool json_pp_number_at(const JsonPP* pp, const char* dot_path, double* out);

/**
 * @brief String value of a line with the quotes removed and \" \\ \/ \n \r \t unescaped (\uXXXX is left as-is).
 *        Output is truncated to @p cap - 1 bytes and always NUL-terminated.
 * @return false if the line is not a string.
 */
bool json_pp_line_string(const JsonPPLine* line, char* out, size_t cap);

#endif
