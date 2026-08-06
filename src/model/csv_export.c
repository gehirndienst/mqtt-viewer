// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "model/csv_export.h"

static bool topic_matches(const char* topic, const char* prefix) {
    if (prefix == NULL || prefix[0] == '\0') return true;

    size_t plen = strlen(prefix);
    if (strncmp(topic, prefix, plen) != 0) return false;

    return topic[plen] == '\0' || topic[plen] == '/';
}

// RFC 4180
static bool write_field(FILE* out, const uint8_t* s, size_t len) {
    bool quoted = memchr(s, ',', len) || memchr(s, '"', len) || memchr(s, '\n', len) || memchr(s, '\r', len);
    if (quoted && fputc('"', out) == EOF) return false;

    for (size_t i = 0; i < len; i++) {
        uint8_t c = s[i] != 0 ? s[i] : (uint8_t)' ';
        if (c == '"' && fputc('"', out) == EOF) return false;
        if (fputc(c, out) == EOF) return false;
    }

    return !quoted || fputc('"', out) != EOF;
}

static bool write_iso8601(FILE* out, uint64_t ts_us) {
    time_t secs = (time_t)(ts_us / 1000000u);
    struct tm tm_utc;
    gmtime_r(&secs, &tm_utc);
    return fprintf(out, "%04d-%02d-%02dT%02d:%02d:%02d.%06uZ", tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
                   tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec, (unsigned)(ts_us % 1000000u)) > 0;
}

int64_t csv_export_write(const MessageBuf* buf, const char* topic_prefix, FILE* out) {
    if (fputs("timestamp,topic,qos,retained,payload\r\n", out) == EOF) return -1;

    int64_t rows = 0;
    uint32_t count = message_buf_count(buf);
    for (uint32_t i = 0; i < count; i++) {
        const MessageRecord* r = message_buf_get(buf, i);
        if (r == NULL || !topic_matches(r->topic, topic_prefix)) continue;
        if (!write_iso8601(out, r->timestamp_us)) return -1;
        if (fputc(',', out) == EOF) return -1;
        if (!write_field(out, (const uint8_t*)r->topic, strlen(r->topic))) return -1;
        if (fprintf(out, ",%u,%u,", (unsigned)r->qos, r->retained ? 1u : 0u) < 0) return -1;
        if (r->payload != NULL && !write_field(out, r->payload, r->payload_len)) return -1;
        if (fputs("\r\n", out) == EOF) return -1;
        rows++;
    }

    return rows;
}
