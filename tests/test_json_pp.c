// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include "model/json_pp.h"
#include "test_helpers.h"

static JsonPP s_pp; // 1.6 MB - keep it off the stack

TEST(nested_object_lines_paths_and_kinds) {
    json_pp_run(&s_pp, "{\"a\": {\"b\": [1, \"2\", true]}, \"c\": \"x\"}");
    // {  a: {  b: [  1  "2"  true  ]  }  c: "x"  }
    ASSERT_EQ(s_pp.line_count, 10);

    ASSERT_STR_EQ(s_pp.lines[0].val, "{");
    ASSERT_EQ(s_pp.lines[0].val_kind, JSON_PP_VAL_PUNCT);
    ASSERT_EQ(s_pp.lines[0].depth, 0);

    ASSERT_STR_EQ(s_pp.lines[1].key, "a");
    ASSERT_STR_EQ(s_pp.lines[1].sep, ": ");
    ASSERT_STR_EQ(s_pp.lines[1].dot_path, "a");
    ASSERT_EQ(s_pp.lines[1].depth, 1);

    ASSERT_STR_EQ(s_pp.lines[3].val, "1");
    ASSERT_EQ(s_pp.lines[3].val_kind, JSON_PP_VAL_ATOM);
    ASSERT_TRUE(s_pp.lines[3].is_numeric);
    ASSERT_STR_EQ(s_pp.lines[3].dot_path, "a.b.0");
    ASSERT_STR_EQ(s_pp.lines[3].trail, ",");

    ASSERT_STR_EQ(s_pp.lines[4].val, "\"2\""); // quotes kept verbatim
    ASSERT_EQ(s_pp.lines[4].val_kind, JSON_PP_VAL_STRING);
    ASSERT_TRUE(s_pp.lines[4].is_numeric); // numeric-looking string is chartable

    ASSERT_STR_EQ(s_pp.lines[5].val, "true");
    ASSERT_FALSE(s_pp.lines[5].is_numeric);
    ASSERT_STR_EQ(s_pp.lines[5].trail, "");

    ASSERT_STR_EQ(s_pp.lines[8].key, "c");
    ASSERT_STR_EQ(s_pp.lines[8].val, "\"x\"");
    ASSERT_FALSE(s_pp.lines[8].is_numeric);
    ASSERT_STR_EQ(s_pp.lines[9].val, "}");
}

TEST(values_are_verbatim_not_reformatted) {
    json_pp_run(&s_pp, "{\"v\": 1.10, \"big\": 12345678901234567890}");
    ASSERT_STR_EQ(s_pp.lines[1].val, "1.10");
    ASSERT_STR_EQ(s_pp.lines[2].val, "12345678901234567890");
}

TEST(truncated_input_still_yields_lines) {
    json_pp_run(&s_pp, "{\"a\": 1, \"b\": {\"c\": 2, \"d\"");
    ASSERT_TRUE(s_pp.line_count >= 4);
    ASSERT_STR_EQ(s_pp.lines[1].key, "a");
    ASSERT_STR_EQ(s_pp.lines[3].key, "c");
}

TEST(malformed_input_terminates) {
    json_pp_run(&s_pp, "{::::,,,,}}}}[[[[");
    ASSERT_TRUE(s_pp.line_count >= 1);
    json_pp_run(&s_pp, "{\"k\" }}}");
    ASSERT_TRUE(s_pp.line_count >= 1);
}

TEST(looks_like_json) {
    ASSERT_TRUE(json_pp_looks_like_json("{\"a\":1}"));
    ASSERT_TRUE(json_pp_looks_like_json("[1,2]"));
    ASSERT_TRUE(json_pp_looks_like_json("\"str\""));
    ASSERT_TRUE(json_pp_looks_like_json(" 42.5 "));
    ASSERT_TRUE(json_pp_looks_like_json("-7"));
    ASSERT_FALSE(json_pp_looks_like_json("0x1F"));
    ASSERT_FALSE(json_pp_looks_like_json("hello"));
    ASSERT_FALSE(json_pp_looks_like_json(""));
    ASSERT_FALSE(json_pp_looks_like_json("12 apples"));
}

TEST(cache_skips_rerun_until_key_changes) {
    static const char src[] = "{\"a\": 1}";
    ASSERT_TRUE(json_pp_run_cached(&s_pp, src, 100));
    ASSERT_FALSE(json_pp_run_cached(&s_pp, src, 100));
    s_pp.lines[1].val[0] = 'X'; // would be overwritten by a rerun
    ASSERT_FALSE(json_pp_run_cached(&s_pp, src, 100));
    ASSERT_EQ(s_pp.lines[1].val[0], 'X');
    ASSERT_TRUE(json_pp_run_cached(&s_pp, src, 101));
    ASSERT_STR_EQ(s_pp.lines[1].val, "1");
    // an uncached run drops the mark
    json_pp_run(&s_pp, src);
    ASSERT_TRUE(json_pp_run_cached(&s_pp, src, 101));
}

int main(void) {
    printf("json_pp tests:\n");
    RUN(nested_object_lines_paths_and_kinds);
    RUN(values_are_verbatim_not_reformatted);
    RUN(truncated_input_still_yields_lines);
    RUN(malformed_input_terminates);
    RUN(looks_like_json);
    RUN(cache_skips_rerun_until_key_changes);
    printf("All json_pp tests passed\n");
    return 0;
}
