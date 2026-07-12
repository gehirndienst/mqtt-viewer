// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include "model/arena_alloc.h"
#include "test_helpers.h"

TEST(create_and_destroy) {
    ArenaAlloc arena;
    arena_alloc_init(&arena, 4096);
    ASSERT_EQ(arena_alloc_used(&arena), 0);
    arena_alloc_destroy(&arena);
}

TEST(alloc_single) {
    ArenaAlloc arena;
    arena_alloc_init(&arena, 4096);
    void* p = arena_alloc_get(&arena, 100);
    ASSERT_NOT_NULL(p);
    memset(p, 0xAB, 100);
    ASSERT_TRUE(arena_alloc_used(&arena) >= 100);
    arena_alloc_destroy(&arena);
}

TEST(alloc_multiple) {
    ArenaAlloc arena;
    arena_alloc_init(&arena, 4096);
    void* a = arena_alloc_get(&arena, 100);
    void* b = arena_alloc_get(&arena, 200);
    void* c = arena_alloc_get(&arena, 300);
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);
    ASSERT_NOT_NULL(c);
    ASSERT_TRUE((uint8_t*)b >= (uint8_t*)a + 100);
    ASSERT_TRUE((uint8_t*)c >= (uint8_t*)b + 200);
    arena_alloc_destroy(&arena);
}

TEST(auto_grow_chunk) {
    ArenaAlloc arena;
    arena_alloc_init(&arena, 64);
    void* a = arena_alloc_get(&arena, 60);
    ASSERT_NOT_NULL(a);
    void* b = arena_alloc_get(&arena, 60);
    ASSERT_NOT_NULL(b);
    memset(a, 1, 60);
    memset(b, 2, 60);
    arena_alloc_destroy(&arena);
}

TEST(reset) {
    ArenaAlloc arena;
    arena_alloc_init(&arena, 4096);
    arena_alloc_get(&arena, 1000);
    arena_alloc_get(&arena, 1000);
    ASSERT_TRUE(arena_alloc_used(&arena) >= 2000);
    arena_alloc_reset(&arena);
    ASSERT_EQ(arena_alloc_used(&arena), 0);
    void* p = arena_alloc_get(&arena, 500);
    ASSERT_NOT_NULL(p);
    arena_alloc_destroy(&arena);
}

TEST(alignment) {
    ArenaAlloc arena;
    arena_alloc_init(&arena, 4096);
    arena_alloc_get(&arena, 1); // 1 byte — forces next alloc to align up
    void* p = arena_alloc_get(&arena, 8);
    ASSERT_EQ((uintptr_t)p % 8, 0);
    arena_alloc_destroy(&arena);
}

int main(void) {
    printf("arena_alloc tests:\n");
    RUN(create_and_destroy);
    RUN(alloc_single);
    RUN(alloc_multiple);
    RUN(auto_grow_chunk);
    RUN(reset);
    RUN(alignment);
    printf("All arena_alloc tests passed\n");
    return 0;
}
