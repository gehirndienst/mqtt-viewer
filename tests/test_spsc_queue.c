// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include "model/spsc_queue.h"
#include "test_helpers.h"

TEST(create_and_destroy) {
    SpscQueue q;
    spsc_queue_init(&q, 16, sizeof(int));
    ASSERT_EQ(spsc_queue_count(&q), 0);
    ASSERT_TRUE(spsc_queue_empty(&q));
    spsc_queue_destroy(&q);
}

TEST(push_pop_single) {
    SpscQueue q;
    spsc_queue_init(&q, 16, sizeof(int));
    int val = 42;
    ASSERT_TRUE(spsc_queue_push(&q, &val));
    ASSERT_EQ(spsc_queue_count(&q), 1);

    int out = 0;
    ASSERT_TRUE(spsc_queue_pop(&q, &out));
    ASSERT_EQ(out, 42);
    ASSERT_TRUE(spsc_queue_empty(&q));
    spsc_queue_destroy(&q);
}

TEST(push_pop_multiple) {
    SpscQueue q;
    spsc_queue_init(&q, 8, sizeof(int));
    for (int i = 0; i < 8; i++) {
        ASSERT_TRUE(spsc_queue_push(&q, &i));
    }
    ASSERT_EQ(spsc_queue_count(&q), 8);

    int overflow = 99;
    ASSERT_FALSE(spsc_queue_push(&q, &overflow));

    for (int i = 0; i < 8; i++) {
        int out;
        ASSERT_TRUE(spsc_queue_pop(&q, &out));
        ASSERT_EQ(out, i);
    }
    ASSERT_TRUE(spsc_queue_empty(&q));
    spsc_queue_destroy(&q);
}

TEST(pop_empty_returns_false) {
    SpscQueue q;
    spsc_queue_init(&q, 4, sizeof(int));
    int out;
    ASSERT_FALSE(spsc_queue_pop(&q, &out));
    spsc_queue_destroy(&q);
}

TEST(wrap_around) {
    SpscQueue q;
    spsc_queue_init(&q, 4, sizeof(int));
    for (int round = 0; round < 2; round++) {
        for (int i = 0; i < 4; i++) {
            int val = round * 10 + i;
            ASSERT_TRUE(spsc_queue_push(&q, &val));
        }
        for (int i = 0; i < 4; i++) {
            int out;
            ASSERT_TRUE(spsc_queue_pop(&q, &out));
            ASSERT_EQ(out, round * 10 + i);
        }
    }
    spsc_queue_destroy(&q);
}

TEST(struct_values) {
    typedef struct {
        int x;
        float y;
    } Pair;
    SpscQueue q;
    spsc_queue_init(&q, 4, sizeof(Pair));
    Pair in = {.x = 10, .y = 3.14f};
    ASSERT_TRUE(spsc_queue_push(&q, &in));
    Pair out;
    ASSERT_TRUE(spsc_queue_pop(&q, &out));
    ASSERT_EQ(out.x, 10);
    ASSERT_TRUE(out.y > 3.13f && out.y < 3.15f);
    spsc_queue_destroy(&q);
}

TEST(drain_batch) {
    SpscQueue q;
    spsc_queue_init(&q, 16, sizeof(int));
    for (int i = 0; i < 10; i++) {
        spsc_queue_push(&q, &i);
    }
    int buf[16];
    uint32_t drained = spsc_queue_drain(&q, buf, 5);
    ASSERT_EQ(drained, 5);
    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(buf[i], i);
    }
    ASSERT_EQ(spsc_queue_count(&q), 5);
    spsc_queue_destroy(&q);
}

int main(void) {
    printf("spsc_queue tests:\n");
    RUN(create_and_destroy);
    RUN(push_pop_single);
    RUN(push_pop_multiple);
    RUN(pop_empty_returns_false);
    RUN(wrap_around);
    RUN(struct_values);
    RUN(drain_batch);
    printf("All spsc_queue tests passed\n");
    return 0;
}
