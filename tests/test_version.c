// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include "model/version.h"
#include "test_helpers.h"

TEST(newer_patch_minor_major) {
    ASSERT_TRUE(version_is_newer("1.0.6", "1.0.5"));
    ASSERT_TRUE(version_is_newer("1.1.0", "1.0.9"));
    ASSERT_TRUE(version_is_newer("2.0.0", "1.9.9"));
}
TEST(equal_or_older_is_not_newer) {
    ASSERT_FALSE(version_is_newer("1.0.5", "1.0.5"));
    ASSERT_FALSE(version_is_newer("1.0.4", "1.0.5"));
    ASSERT_FALSE(version_is_newer("0.9.9", "1.0.0"));
}
TEST(numeric_not_lexical) {
    ASSERT_TRUE(version_is_newer("1.0.10", "1.0.9"));
    ASSERT_FALSE(version_is_newer("1.0.9", "1.0.10"));
}
TEST(leading_v_is_ignored) {
    ASSERT_TRUE(version_is_newer("v1.0.6", "1.0.5"));
    ASSERT_FALSE(version_is_newer("v1.0.5", "1.0.5"));
}
TEST(missing_parts_count_as_zero) {
    ASSERT_TRUE(version_is_newer("1.1", "1.0.5"));
    ASSERT_FALSE(version_is_newer("1.0", "1.0.0"));
}
TEST(prerelease_is_older_than_release) {
    ASSERT_TRUE(version_is_newer("1.0.5", "1.0.5-rc1"));
    ASSERT_FALSE(version_is_newer("1.0.5-rc1", "1.0.5"));
    ASSERT_TRUE(version_is_newer("1.0.6-rc1", "1.0.5"));
}
TEST(unparseable_is_never_newer) {
    ASSERT_FALSE(version_is_newer("", "1.0.5"));
    ASSERT_FALSE(version_is_newer("latest", "1.0.5"));
    ASSERT_FALSE(version_is_newer("1.0.6", "garbage"));
    ASSERT_FALSE(version_is_newer(NULL, "1.0.5"));
}

TEST(extract_tag_from_github_json) {
    char tag[32];
    const char* json = "{\"url\":\"x\",\"tag_name\":\"v1.0.6\",\"name\":\"1.0.6\"}";
    ASSERT_TRUE(version_extract_tag(json, tag, sizeof(tag)));
    ASSERT_STR_EQ(tag, "v1.0.6");
}
TEST(extract_tag_tolerates_whitespace) {
    char tag[32];
    ASSERT_TRUE(version_extract_tag("{ \"tag_name\" :  \"1.2.3\" }", tag, sizeof(tag)));
    ASSERT_STR_EQ(tag, "1.2.3");
}
TEST(extract_tag_rejects_bad_input) {
    char tag[8];
    ASSERT_FALSE(version_extract_tag("{\"message\":\"Not Found\"}", tag, sizeof(tag)));
    ASSERT_FALSE(version_extract_tag("", tag, sizeof(tag)));
    ASSERT_FALSE(version_extract_tag(NULL, tag, sizeof(tag)));
    ASSERT_FALSE(version_extract_tag("{\"tag_name\":\"v1.0.6", tag, sizeof(tag))); // unterminated
    ASSERT_FALSE(version_extract_tag("{\"tag_name\":\"v1.0.123456789\"}", tag, sizeof(tag))); // too long
}

int main(void) {
    printf("version tests\n");
    RUN(newer_patch_minor_major);
    RUN(equal_or_older_is_not_newer);
    RUN(numeric_not_lexical);
    RUN(leading_v_is_ignored);
    RUN(missing_parts_count_as_zero);
    RUN(prerelease_is_older_than_release);
    RUN(unparseable_is_never_newer);
    RUN(extract_tag_from_github_json);
    RUN(extract_tag_tolerates_whitespace);
    RUN(extract_tag_rejects_bad_input);
    printf("All tests passed\n");
    return 0;
}
