// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include "model/csv_export.h"
#include "test_helpers.h"

#define CSV_HEADER "timestamp,topic,qos,retained,payload\r\n"

static void export_to_string(const MessageBuf* buf, const char* prefix, char* out, size_t out_size, int64_t* rows) {
    FILE* f = tmpfile();
    ASSERT_NOT_NULL(f);
    *rows = csv_export_write(buf, prefix, f);
    rewind(f);
    size_t n = fread(out, 1, out_size - 1, f);
    out[n] = '\0';
    fclose(f);
}

static void push_text(MessageBuf* buf, uint64_t ts, const char* topic, const char* payload) {
    MessageRecord rec = {.timestamp_us = ts, .qos = 1, .retained = false};
    snprintf(rec.topic, sizeof(rec.topic), "%s", topic);
    rec.payload = (uint8_t*)payload;
    rec.payload_len = (uint32_t)strlen(payload);
    message_buf_push(buf, &rec);
}

TEST(empty_buffer_writes_header_only) {
    MessageBuf buf;
    message_buf_init(&buf, 4);
    char out[512];
    int64_t rows;
    export_to_string(&buf, NULL, out, sizeof(out), &rows);
    ASSERT_EQ(rows, 0);
    ASSERT_STR_EQ(out, CSV_HEADER);
    message_buf_destroy(&buf);
}

TEST(plain_text_row) {
    MessageBuf buf;
    message_buf_init(&buf, 4);
    push_text(&buf, 0, "sensors/temp", "21.5");
    char out[512];
    int64_t rows;
    export_to_string(&buf, NULL, out, sizeof(out), &rows);
    ASSERT_EQ(rows, 1);
    ASSERT_STR_EQ(out, CSV_HEADER "1970-01-01T00:00:00.000000Z,sensors/temp,1,0,21.5\r\n");
    message_buf_destroy(&buf);
}

TEST(quoting_comma_quote_newline) {
    MessageBuf buf;
    message_buf_init(&buf, 4);
    push_text(&buf, 0, "t", "a,b");
    push_text(&buf, 0, "t", "say \"hi\"");
    push_text(&buf, 0, "t", "line1\nline2");
    char out[1024];
    int64_t rows;
    export_to_string(&buf, NULL, out, sizeof(out), &rows);
    ASSERT_EQ(rows, 3);
    ASSERT_NOT_NULL(strstr(out, ",\"a,b\"\r\n"));
    ASSERT_NOT_NULL(strstr(out, ",\"say \"\"hi\"\"\"\r\n"));
    ASSERT_NOT_NULL(strstr(out, ",\"line1\nline2\"\r\n"));
    message_buf_destroy(&buf);
}

TEST(utf8_payload_passes_through) {
    MessageBuf buf;
    message_buf_init(&buf, 4);
    push_text(&buf, 0, "t", "gr\xc3\xbc\xc3\x9f\x65 21\xc2\xb0");
    char out[512];
    int64_t rows;
    export_to_string(&buf, NULL, out, sizeof(out), &rows);
    ASSERT_EQ(rows, 1);
    ASSERT_NOT_NULL(strstr(out, ",gr\xc3\xbc\xc3\x9f\x65 21\xc2\xb0\r\n"));
    message_buf_destroy(&buf);
}

TEST(nul_bytes_become_spaces) {
    MessageBuf buf;
    message_buf_init(&buf, 4);
    MessageRecord rec = {0};
    snprintf(rec.topic, sizeof(rec.topic), "t");
    uint8_t data[3] = {'a', 0x00, 'b'};
    rec.payload = data;
    rec.payload_len = 3;
    message_buf_push(&buf, &rec);
    char out[512];
    int64_t rows;
    export_to_string(&buf, NULL, out, sizeof(out), &rows);
    ASSERT_EQ(rows, 1);
    ASSERT_NOT_NULL(strstr(out, ",a b\r\n"));
    message_buf_destroy(&buf);
}

TEST(prefix_filter_segment_boundary) {
    MessageBuf buf;
    message_buf_init(&buf, 8);
    push_text(&buf, 0, "a/b", "1");
    push_text(&buf, 0, "a/b/c", "2");
    push_text(&buf, 0, "a/bc", "3");
    push_text(&buf, 0, "other", "4");
    char out[1024];
    int64_t rows;
    export_to_string(&buf, "a/b", out, sizeof(out), &rows);
    ASSERT_EQ(rows, 2);
    ASSERT_NOT_NULL(strstr(out, ",a/b,"));
    ASSERT_NOT_NULL(strstr(out, ",a/b/c,"));
    ASSERT_NULL(strstr(out, ",a/bc,"));
    ASSERT_NULL(strstr(out, ",other,"));
    message_buf_destroy(&buf);
}

TEST(no_match_writes_header_only) {
    MessageBuf buf;
    message_buf_init(&buf, 4);
    push_text(&buf, 0, "x", "1");
    char out[512];
    int64_t rows;
    export_to_string(&buf, "nope", out, sizeof(out), &rows);
    ASSERT_EQ(rows, 0);
    ASSERT_STR_EQ(out, CSV_HEADER);
    message_buf_destroy(&buf);
}

TEST(ring_wrap_exports_oldest_first) {
    MessageBuf buf;
    message_buf_init(&buf, 2);
    push_text(&buf, 100, "t", "first");
    push_text(&buf, 200, "t", "second");
    push_text(&buf, 300, "t", "third");
    char out[1024];
    int64_t rows;
    export_to_string(&buf, NULL, out, sizeof(out), &rows);
    ASSERT_EQ(rows, 2);
    char* second = strstr(out, "second");
    char* third = strstr(out, "third");
    ASSERT_NOT_NULL(second);
    ASSERT_NOT_NULL(third);
    ASSERT_TRUE(second < third);
    ASSERT_NULL(strstr(out, "first"));
    message_buf_destroy(&buf);
}

TEST(iso_timestamp_formatting) {
    MessageBuf buf;
    message_buf_init(&buf, 4);
    push_text(&buf, 1785888000123456ULL, "t", "x");
    char out[512];
    int64_t rows;
    export_to_string(&buf, NULL, out, sizeof(out), &rows);
    ASSERT_EQ(rows, 1);
    ASSERT_NOT_NULL(strstr(out, "2026-08-05T00:00:00.123456Z,"));
    message_buf_destroy(&buf);
}

TEST(write_error_returns_minus_one) {
    const char* path = "csv_export_readonly_test.csv";
    FILE* setup = fopen(path, "w");
    ASSERT_NOT_NULL(setup);
    fclose(setup);

    MessageBuf buf;
    message_buf_init(&buf, 4);
    push_text(&buf, 0, "t", "x");
    FILE* f = fopen(path, "r");
    ASSERT_NOT_NULL(f);
    int64_t rows = csv_export_write(&buf, NULL, f);
    ASSERT_EQ(rows, -1);
    fclose(f);
    remove(path);
    message_buf_destroy(&buf);
}

int main(void) {
    printf("csv_export tests:\n");
    RUN(empty_buffer_writes_header_only);
    RUN(plain_text_row);
    RUN(quoting_comma_quote_newline);
    RUN(utf8_payload_passes_through);
    RUN(nul_bytes_become_spaces);
    RUN(prefix_filter_segment_boundary);
    RUN(no_match_writes_header_only);
    RUN(ring_wrap_exports_oldest_first);
    RUN(iso_timestamp_formatting);
    RUN(write_error_returns_minus_one);
    printf("All csv_export tests passed.\n");
    return 0;
}
