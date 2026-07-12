#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST(name) static void test_##name(void)
#define RUN(name) \
    do { \
        printf("  %-50s", #name); \
        fflush(stdout); \
        test_##name(); \
        printf("PASS\n"); \
    } while (0)

#define ASSERT_EQ(a, b) assert((a) == (b))
#define ASSERT_NE(a, b) assert((a) != (b))
#define ASSERT_TRUE(x) assert((x))
#define ASSERT_FALSE(x) assert(!(x))
#define ASSERT_NULL(x) assert((x) == NULL)
#define ASSERT_NOT_NULL(x) assert((x) != NULL)
#define ASSERT_STR_EQ(a, b) assert(strcmp((a), (b)) == 0)

#endif
