// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include "model/subscription.h"
#include "test_helpers.h"

TEST(exact_match) {
    ASSERT_TRUE(mqtt_topic_matches("a/b/c", "a/b/c"));
    ASSERT_FALSE(mqtt_topic_matches("a/b/c", "a/b/d"));
}
TEST(hash_wildcard) {
    ASSERT_TRUE(mqtt_topic_matches("#", "a/b/c"));
    ASSERT_TRUE(mqtt_topic_matches("a/#", "a/b/c"));
    ASSERT_TRUE(mqtt_topic_matches("a/#", "a"));
    ASSERT_FALSE(mqtt_topic_matches("#", "$SYS/info"));
}
TEST(plus_wildcard) {
    ASSERT_TRUE(mqtt_topic_matches("a/+/c", "a/b/c"));
    ASSERT_TRUE(mqtt_topic_matches("+/b/c", "a/b/c"));
    ASSERT_FALSE(mqtt_topic_matches("a/+/c", "a/b/d"));
    ASSERT_FALSE(mqtt_topic_matches("+/b", "$SYS/b"));
}
TEST(dollar_topics) {
    ASSERT_FALSE(mqtt_topic_matches("#", "$SYS/monitor"));
    ASSERT_FALSE(mqtt_topic_matches("+", "$SYS"));
    ASSERT_TRUE(mqtt_topic_matches("$SYS/#", "$SYS/monitor"));
    ASSERT_TRUE(mqtt_topic_matches("$SYS/monitor", "$SYS/monitor"));
}

TEST(plus_no_multilevel) {
    // + must not match multiple levels
    ASSERT_FALSE(mqtt_topic_matches("a/+", "a/b/c"));
    ASSERT_TRUE(mqtt_topic_matches("a/+", "a/b"));
}
TEST(dollar_nested_wildcard) {
    // Nested wildcards in explicit $SYS/ filters should work
    ASSERT_TRUE(mqtt_topic_matches("$SYS/+", "$SYS/monitor"));
    ASSERT_TRUE(mqtt_topic_matches("$SYS/#", "$SYS/a/b/c"));
}

int main(void) {
    printf("subscription tests\n");
    RUN(exact_match);
    RUN(hash_wildcard);
    RUN(plus_wildcard);
    RUN(dollar_topics);
    RUN(plus_no_multilevel);
    RUN(dollar_nested_wildcard);
    printf("All tests passed\n");
    return 0;
}
