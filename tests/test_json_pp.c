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

TEST(run_len_stops_at_length_not_nul) {
    const char raw[] = "{\"v\": 5}GARBAGE";
    json_pp_run_len(&s_pp, raw, 8);
    ASSERT_EQ(s_pp.line_count, 3);
    ASSERT_STR_EQ(s_pp.lines[1].val, "5");
}

TEST(line_number_handles_atoms_and_numeric_strings) {
    json_pp_run(&s_pp, "{\"a\": 1.5, \"b\": \"2e3\", \"c\": \"x\", \"d\": true}");
    double v = 0;
    ASSERT_TRUE(json_pp_line_number(&s_pp.lines[1], &v));
    ASSERT_TRUE(v == 1.5);
    ASSERT_TRUE(json_pp_line_number(&s_pp.lines[2], &v));
    ASSERT_TRUE(v == 2000.0);
    ASSERT_FALSE(json_pp_line_number(&s_pp.lines[3], &v));
    ASSERT_FALSE(json_pp_line_number(&s_pp.lines[4], &v));
    ASSERT_FALSE(json_pp_line_number(&s_pp.lines[0], &v)); // "{"
}

TEST(line_string_unquotes_and_unescapes) {
    json_pp_run(&s_pp, "[\"plain\", \"q\\\"uote\\\\back\\/slash\\n\", 42]");
    char out[64];
    ASSERT_TRUE(json_pp_line_string(&s_pp.lines[1], out, sizeof(out)));
    ASSERT_STR_EQ(out, "plain");
    ASSERT_TRUE(json_pp_line_string(&s_pp.lines[2], out, sizeof(out)));
    ASSERT_STR_EQ(out, "q\"uote\\back/slash\n");
    ASSERT_FALSE(json_pp_line_string(&s_pp.lines[3], out, sizeof(out))); // 42 is not a string
    char tiny[4];
    ASSERT_TRUE(json_pp_line_string(&s_pp.lines[1], tiny, sizeof(tiny)));
    ASSERT_STR_EQ(tiny, "pla");
}

TEST(find_by_dot_path) {
    json_pp_run(&s_pp, "{\"a\": {\"b\": [10, 20]}, \"c\": 3}");
    const JsonPPLine* l = json_pp_find(&s_pp, "a.b.1");
    ASSERT_NOT_NULL(l);
    ASSERT_STR_EQ(l->val, "20");
    l = json_pp_find(&s_pp, "c");
    ASSERT_NOT_NULL(l);
    ASSERT_STR_EQ(l->val, "3");
    ASSERT_NULL(json_pp_find(&s_pp, "a.b.2"));
    ASSERT_NULL(json_pp_find(&s_pp, "zzz"));
    // the root scalar is reachable as ""
    json_pp_run(&s_pp, "42");
    l = json_pp_find(&s_pp, "");
    ASSERT_NOT_NULL(l);
    ASSERT_STR_EQ(l->val, "42");
}

int main(void) {
    printf("json_pp tests:\n");
    RUN(nested_object_lines_paths_and_kinds);
    RUN(values_are_verbatim_not_reformatted);
    RUN(truncated_input_still_yields_lines);
    RUN(malformed_input_terminates);
    RUN(looks_like_json);
    RUN(cache_skips_rerun_until_key_changes);
    RUN(run_len_stops_at_length_not_nul);
    RUN(line_number_handles_atoms_and_numeric_strings);
    RUN(line_string_unquotes_and_unescapes);
    RUN(find_by_dot_path);
    printf("All json_pp tests passed\n");
    return 0;
}
