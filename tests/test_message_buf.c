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

int main(void) {
    printf("message_buf tests:\n");
    RUN(create_and_destroy);
    RUN(push_and_get);
    RUN(ring_buffer_eviction);
    RUN(clear);
    printf("All message_buf tests passed.\n");
    return 0;
}
