// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include "model/message_buf.h"
#include "test_helpers.h"

TEST(create_and_destroy) {
    MessageBuf buf;
    message_buf_init(&buf, 8);
    ASSERT_EQ(message_buf_count(&buf), 0);
    message_buf_destroy(&buf);
}

TEST(push_and_get) {
    MessageBuf buf;
    message_buf_init(&buf, 8);
    MessageRecord rec = {
        .timestamp_us = 1000,
        .qos = 1,
        .retained = false,
        .payload_len = 5,
    };
    message_buf_push(&buf, &rec);
    ASSERT_EQ(message_buf_count(&buf), 1);
    const MessageRecord* got = message_buf_get(&buf, 0);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->timestamp_us, 1000);
    ASSERT_EQ(got->qos, 1);
    message_buf_destroy(&buf);
}

TEST(ring_buffer_eviction) {
    MessageBuf buf;
    message_buf_init(&buf, 4);
    for (int i = 0; i < 6; i++) {
        MessageRecord rec = {.timestamp_us = (uint64_t)i * 100};
        message_buf_push(&buf, &rec);
    }
    ASSERT_EQ(message_buf_count(&buf), 4);
    const MessageRecord* oldest = message_buf_get(&buf, 0);
    ASSERT_EQ(oldest->timestamp_us, 200);
    const MessageRecord* newest = message_buf_get(&buf, 3);
    ASSERT_EQ(newest->timestamp_us, 500);
    message_buf_destroy(&buf);
}

TEST(clear) {
    MessageBuf buf;
    message_buf_init(&buf, 8);
    MessageRecord rec = {.timestamp_us = 42};
    message_buf_push(&buf, &rec);
    message_buf_clear(&buf);
    ASSERT_EQ(message_buf_count(&buf), 0);
    message_buf_destroy(&buf);
}

TEST(payload_copied_on_push) {
    MessageBuf buf;
    message_buf_init(&buf, 4);
    uint8_t src[3] = {1, 2, 3};
    MessageRecord rec = {.payload = src, .payload_len = 3};
    message_buf_push(&buf, &rec);
    src[0] = 99; // mutate
    const MessageRecord* got = message_buf_get(&buf, 0);
    ASSERT_NOT_NULL(got);
    ASSERT_NOT_NULL(got->payload);
    ASSERT_NE((const void*)got->payload, (const void*)src);
    ASSERT_EQ(got->payload[0], 1);
    ASSERT_EQ(got->payload[2], 3);
    message_buf_destroy(&buf);
}

TEST(null_and_empty_payload_stored_as_null) {
    MessageBuf buf;
    message_buf_init(&buf, 4);
    MessageRecord rec_null = {.payload = NULL, .payload_len = 0};
    message_buf_push(&buf, &rec_null);
    uint8_t one = 7;
    MessageRecord rec_zero_len = {.payload = &one, .payload_len = 0};
    message_buf_push(&buf, &rec_zero_len);
    ASSERT_NULL(message_buf_get(&buf, 0)->payload);
    ASSERT_NULL(message_buf_get(&buf, 1)->payload);
    message_buf_destroy(&buf);
}

TEST(eviction_and_clear_free_payloads) {
    MessageBuf buf;
    message_buf_init(&buf, 2);
    for (int i = 0; i < 5; i++) {
        uint8_t byte = (uint8_t)i;
        MessageRecord rec = {.timestamp_us = (uint64_t)i, .payload = &byte, .payload_len = 1};
        message_buf_push(&buf, &rec);
    }
    ASSERT_EQ(message_buf_count(&buf), 2);
    ASSERT_EQ(message_buf_get(&buf, 0)->payload[0], 3);
    ASSERT_EQ(message_buf_get(&buf, 1)->payload[0], 4);
    message_buf_clear(&buf);
    ASSERT_EQ(message_buf_count(&buf), 0);
    uint8_t after = 42;
    MessageRecord rec = {.payload = &after, .payload_len = 1};
    message_buf_push(&buf, &rec);
    ASSERT_EQ(message_buf_get(&buf, 0)->payload[0], 42);
    message_buf_destroy(&buf);
}

int main(void) {
    printf("message_buf tests:\n");
    RUN(create_and_destroy);
    RUN(push_and_get);
    RUN(ring_buffer_eviction);
    RUN(clear);
    RUN(payload_copied_on_push);
    RUN(null_and_empty_payload_stored_as_null);
    RUN(eviction_and_clear_free_payloads);
    printf("All message_buf tests passed.\n");
    return 0;
}
