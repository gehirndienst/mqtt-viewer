// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include "model/pool_alloc.h"
#include "test_helpers.h"

TEST(create_and_destroy) {
    PoolAlloc pool;
    pool_alloc_init(&pool, sizeof(int), 64);
    ASSERT_EQ(pool_alloc_used(&pool), 0);
    pool_alloc_destroy(&pool);
}

TEST(alloc_single) {
    PoolAlloc pool;
    pool_alloc_init(&pool, sizeof(int), 64);
    int* p = pool_alloc_get(&pool);
    ASSERT_NOT_NULL(p);
    *p = 42;
    ASSERT_EQ(*p, 42);
    ASSERT_EQ(pool_alloc_used(&pool), 1);
    pool_alloc_destroy(&pool);
}

TEST(alloc_and_free) {
    PoolAlloc pool;
    pool_alloc_init(&pool, sizeof(int), 64);
    int* p = pool_alloc_get(&pool);
    *p = 10;
    pool_alloc_put(&pool, p);
    ASSERT_EQ(pool_alloc_used(&pool), 0);

    int* q = pool_alloc_get(&pool);
    ASSERT_NOT_NULL(q);
    ASSERT_EQ(q, p); // free-list must return same address
    ASSERT_EQ(pool_alloc_used(&pool), 1);
    pool_alloc_destroy(&pool);
}

TEST(alloc_fills_slab) {
    PoolAlloc pool;
    pool_alloc_init(&pool, sizeof(int), 4);
    int* ptrs[4];
    for (int i = 0; i < 4; i++) {
        ptrs[i] = pool_alloc_get(&pool);
        ASSERT_NOT_NULL(ptrs[i]);
        *ptrs[i] = i;
    }
    ASSERT_EQ(pool_alloc_used(&pool), 4);
    for (int i = 0; i < 4; i++) {
        ASSERT_EQ(*ptrs[i], i);
    }
    pool_alloc_destroy(&pool);
}

TEST(auto_grow) {
    PoolAlloc pool;
    pool_alloc_init(&pool, sizeof(int), 4);
    int* ptrs[12];
    for (int i = 0; i < 12; i++) {
        ptrs[i] = pool_alloc_get(&pool);
        ASSERT_NOT_NULL(ptrs[i]);
        *ptrs[i] = i * 100;
    }
    for (int i = 0; i < 12; i++) {
        ASSERT_EQ(*ptrs[i], i * 100);
    }
    ASSERT_EQ(pool_alloc_used(&pool), 12);
    pool_alloc_destroy(&pool);
}

TEST(free_and_realloc_pattern) {
    PoolAlloc pool;
    pool_alloc_init(&pool, sizeof(int), 8);
    int* a = pool_alloc_get(&pool);
    int* b = pool_alloc_get(&pool);
    int* c = pool_alloc_get(&pool);
    pool_alloc_put(&pool, b);
    ASSERT_EQ(pool_alloc_used(&pool), 2);
    int* d = pool_alloc_get(&pool);
    ASSERT_NOT_NULL(d);
    ASSERT_EQ(d, b); // free-list returned b's slot
    ASSERT_NE(d, a); // does not overlap live allocation a
    ASSERT_NE(d, c); // does not overlap live allocation c
    ASSERT_EQ(pool_alloc_used(&pool), 3);
    pool_alloc_destroy(&pool);
}

int main(void) {
    printf("pool_alloc tests:\n");
    RUN(create_and_destroy);
    RUN(alloc_single);
    RUN(alloc_and_free);
    RUN(alloc_fills_slab);
    RUN(auto_grow);
    RUN(free_and_realloc_pattern);
    printf("All pool_alloc tests passed\n");
    return 0;
}
