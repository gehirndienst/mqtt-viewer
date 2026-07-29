#include "test_helpers.h"
#include "ui/text_input.h"

TEST(append_simple) {
    char buf[16] = "ab";
    text_input_append_filtered(buf, sizeof(buf), "cd", false);
    ASSERT_STR_EQ(buf, "abcd");
}

TEST(append_into_empty) {
    char buf[16] = "";
    text_input_append_filtered(buf, sizeof(buf), "host.example", false);
    ASSERT_STR_EQ(buf, "host.example");
}

TEST(truncates_at_capacity) {
    char buf[6] = "ab";
    text_input_append_filtered(buf, sizeof(buf), "cdefgh", false);
    ASSERT_STR_EQ(buf, "abcde");
}

TEST(strips_control_chars) {
    char buf[32] = "";
    text_input_append_filtered(buf, sizeof(buf), "pa\tss\r\nword\x01!", false);
    ASSERT_STR_EQ(buf, "password!");
}

TEST(keeps_newlines_when_allowed) {
    char buf[32] = "";
    text_input_append_filtered(buf, sizeof(buf), "{\r\n \"a\": 1\r\n}", true);
    ASSERT_STR_EQ(buf, "{\n \"a\": 1\n}");
}

TEST(null_or_empty_src_is_noop) {
    char buf[8] = "ab";
    text_input_append_filtered(buf, sizeof(buf), NULL, false);
    ASSERT_STR_EQ(buf, "ab");
    text_input_append_filtered(buf, sizeof(buf), "", false);
    ASSERT_STR_EQ(buf, "ab");
}

TEST(keeps_utf8_bytes) {
    char buf[16] = "";
    text_input_append_filtered(buf, sizeof(buf), "caf\xc3\xa9", false);
    ASSERT_STR_EQ(buf, "caf\xc3\xa9");
}

int main(void) {
    printf("test_text_input:\n");
    RUN(append_simple);
    RUN(append_into_empty);
    RUN(truncates_at_capacity);
    RUN(strips_control_chars);
    RUN(keeps_newlines_when_allowed);
    RUN(null_or_empty_src_is_noop);
    RUN(keeps_utf8_bytes);
    printf("all tests passed\n");
    return 0;
}
