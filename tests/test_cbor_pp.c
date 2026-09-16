// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <string.h>

#include "model/cbor_pp.h"
#include "test_helpers.h"

static JsonPP s_pp;
static uint8_t s_buf[4096];
static size_t s_len;

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool run_hex(const char* hex) {
    s_len = 0;
    for (const char* p = hex; *p;) {
        if (*p == ' ') {
            p++;
            continue;
        }
        s_buf[s_len++] = (uint8_t)(hexval(p[0]) * 16 + hexval(p[1]));
        p += 2;
    }
    return cbor_pp_run(&s_pp, s_buf, s_len);
}

static size_t build_string(uint8_t major, size_t n) {
    size_t p = 0;
    s_buf[p++] = (uint8_t)((major << 5) | 25);
    s_buf[p++] = (uint8_t)(n >> 8);
    s_buf[p++] = (uint8_t)(n & 0xff);
    for (size_t i = 0; i < n; i++) s_buf[p++] = (uint8_t)('a' + (i % 26));
    return p;
}

#define ASSERT_SCALAR(hex, text) \
    do { \
        ASSERT_TRUE(run_hex(hex)); \
        ASSERT_EQ(s_pp.line_count, 1); \
        ASSERT_STR_EQ(s_pp.lines[0].val, text); \
        ASSERT_STR_EQ(s_pp.lines[0].dot_path, ""); \
        ASSERT_EQ(s_pp.lines[0].depth, 0); \
    } while (0)

TEST(appendix_a_integers) {
    ASSERT_SCALAR("00", "0");
    ASSERT_SCALAR("01", "1");
    ASSERT_SCALAR("0a", "10");
    ASSERT_SCALAR("17", "23");
    ASSERT_SCALAR("18 18", "24");
    ASSERT_SCALAR("18 64", "100");
    ASSERT_SCALAR("19 03 e8", "1000");
    ASSERT_SCALAR("1a 00 0f 42 40", "1000000");
    ASSERT_SCALAR("1b 00 00 00 e8 d4 a5 10 00", "1000000000000");
    ASSERT_SCALAR("1b ff ff ff ff ff ff ff ff", "18446744073709551615");
    ASSERT_SCALAR("3b ff ff ff ff ff ff ff ff", "-18446744073709551616");
    ASSERT_SCALAR("20", "-1");
    ASSERT_SCALAR("29", "-10");
    ASSERT_SCALAR("38 63", "-100");
    ASSERT_SCALAR("39 03 e7", "-1000");
    ASSERT_EQ(s_pp.lines[0].val_kind, JSON_PP_VAL_ATOM);
    ASSERT_TRUE(s_pp.lines[0].is_numeric);
}

TEST(appendix_a_floats) {
    ASSERT_SCALAR("f9 00 00", "0.0");
    ASSERT_SCALAR("f9 80 00", "-0.0");
    ASSERT_SCALAR("f9 3c 00", "1.0");
    ASSERT_SCALAR("fb 3f f1 99 99 99 99 99 9a", "1.1");
    ASSERT_SCALAR("f9 3e 00", "1.5");
    ASSERT_SCALAR("f9 7b ff", "65504.0");
    ASSERT_SCALAR("fa 47 c3 50 00", "100000.0");
    ASSERT_SCALAR("fa 7f 7f ff ff", "3.4028234663852886e+38");
    ASSERT_SCALAR("fb 7e 37 e4 3c 88 00 75 9c", "1e+300");
    ASSERT_SCALAR("f9 00 01", "5.9604644775390625e-08");
    ASSERT_SCALAR("f9 04 00", "6.103515625e-05");
    ASSERT_SCALAR("f9 c4 00", "-4.0");
    ASSERT_SCALAR("fb c0 10 66 66 66 66 66 66", "-4.1");
    ASSERT_TRUE(s_pp.lines[0].is_numeric);
    ASSERT_SCALAR("f9 7c 00", "Infinity");
    ASSERT_SCALAR("f9 7e 00", "NaN");
    ASSERT_SCALAR("f9 fc 00", "-Infinity");
    ASSERT_SCALAR("fa 7f 80 00 00", "Infinity");
    ASSERT_SCALAR("fa 7f c0 00 00", "NaN");
    ASSERT_SCALAR("fa ff 80 00 00", "-Infinity");
    ASSERT_SCALAR("fb 7f f0 00 00 00 00 00 00", "Infinity");
    ASSERT_SCALAR("fb 7f f8 00 00 00 00 00 00", "NaN");
    ASSERT_SCALAR("fb ff f0 00 00 00 00 00 00", "-Infinity");
    ASSERT_FALSE(s_pp.lines[0].is_numeric); // non-finite is not chartable
}

TEST(appendix_a_simple_values_and_tags) {
    ASSERT_SCALAR("f4", "false");
    ASSERT_SCALAR("f5", "true");
    ASSERT_SCALAR("f6", "null");
    ASSERT_SCALAR("f7", "undefined");
    ASSERT_FALSE(s_pp.lines[0].is_numeric);
    ASSERT_SCALAR("f0", "simple(16)");
    ASSERT_SCALAR("f8 ff", "simple(255)");
    ASSERT_SCALAR("c0 74 32 30 31 33 2d 30 33 2d 32 31 54 32 30 3a 30 34 3a 30 30 5a", "0(\"2013-03-21T20:04:00Z\")");
    ASSERT_SCALAR("c1 1a 51 4b 67 b0", "1(1363896240)");
    ASSERT_FALSE(s_pp.lines[0].is_numeric);
    ASSERT_SCALAR("c1 fb 41 d4 52 d9 ec 20 00 00", "1(1363896240.5)");
    ASSERT_SCALAR("d7 44 01 02 03 04", "23(h'01020304')");
    ASSERT_SCALAR("d8 18 45 64 49 45 54 46", "24(h'6449455446')");
    ASSERT_SCALAR("d8 20 76 68 74 74 70 3a 2f 2f 77 77 77 2e 65 78 61 6d 70 6c 65 2e 63 6f 6d",
                  "32(\"http://www.example.com\")");
    ASSERT_SCALAR("c2 49 01 00 00 00 00 00 00 00 00", "2(h'010000000000000000')");
    ASSERT_SCALAR("c3 49 01 00 00 00 00 00 00 00 00", "3(h'010000000000000000')");
}

TEST(appendix_a_strings) {
    ASSERT_SCALAR("40", "h''");
    ASSERT_SCALAR("44 01 02 03 04", "h'01020304'");
    ASSERT_EQ(s_pp.lines[0].val_kind, JSON_PP_VAL_ATOM);
    ASSERT_SCALAR("60", "\"\"");
    ASSERT_SCALAR("61 61", "\"a\"");
    ASSERT_SCALAR("64 49 45 54 46", "\"IETF\"");
    ASSERT_EQ(s_pp.lines[0].val_kind, JSON_PP_VAL_STRING);
    ASSERT_FALSE(s_pp.lines[0].is_numeric);
    ASSERT_SCALAR("62 22 5c", "\"\\\"\\\\\"");
    ASSERT_SCALAR("62 c3 bc", "\"\xc3\xbc\"");
    ASSERT_SCALAR("63 e6 b0 b4", "\"\xe6\xb0\xb4\"");
    ASSERT_SCALAR("64 f0 90 85 91", "\"\xf0\x90\x85\x91\"");
    ASSERT_SCALAR("62 0a 09", "\"\\n\\t\"");
    ASSERT_SCALAR("61 01", "\"\\u0001\"");
    ASSERT_SCALAR("64 31 32 2e 35", "\"12.5\"");
    ASSERT_TRUE(s_pp.lines[0].is_numeric);
    char out[16];
    ASSERT_TRUE(json_pp_line_string(&s_pp.lines[0], out, sizeof(out)));
    ASSERT_STR_EQ(out, "12.5");
}

TEST(appendix_a_arrays_and_maps) {
    ASSERT_TRUE(run_hex("80"));
    ASSERT_EQ(s_pp.line_count, 2);
    ASSERT_STR_EQ(s_pp.lines[0].val, "[");
    ASSERT_STR_EQ(s_pp.lines[1].val, "]");
    ASSERT_EQ(s_pp.lines[0].val_kind, JSON_PP_VAL_PUNCT);

    ASSERT_TRUE(run_hex("83 01 02 03"));
    ASSERT_EQ(s_pp.line_count, 5);
    ASSERT_STR_EQ(s_pp.lines[1].val, "1");
    ASSERT_STR_EQ(s_pp.lines[1].trail, ",");
    ASSERT_STR_EQ(s_pp.lines[1].dot_path, "0");
    ASSERT_EQ(s_pp.lines[1].depth, 1);
    ASSERT_STR_EQ(s_pp.lines[3].val, "3");
    ASSERT_STR_EQ(s_pp.lines[3].trail, "");
    ASSERT_STR_EQ(s_pp.lines[3].dot_path, "2");

    ASSERT_TRUE(run_hex("83 01 82 02 03 82 04 05"));
    ASSERT_EQ(s_pp.line_count, 11);
    ASSERT_STR_EQ(s_pp.lines[2].val, "[");
    ASSERT_STR_EQ(s_pp.lines[2].dot_path, "1");
    ASSERT_STR_EQ(s_pp.lines[3].dot_path, "1.0");
    ASSERT_EQ(s_pp.lines[3].depth, 2);
    ASSERT_STR_EQ(s_pp.lines[5].val, "]");
    ASSERT_STR_EQ(s_pp.lines[5].trail, ",");
    ASSERT_STR_EQ(s_pp.lines[9].val, "]");
    ASSERT_STR_EQ(s_pp.lines[9].trail, "");
    ASSERT_STR_EQ(s_pp.lines[10].val, "]");
    ASSERT_EQ(s_pp.lines[10].depth, 0);

    ASSERT_TRUE(run_hex("a0"));
    ASSERT_EQ(s_pp.line_count, 2);
    ASSERT_STR_EQ(s_pp.lines[0].val, "{");
    ASSERT_STR_EQ(s_pp.lines[1].val, "}");

    ASSERT_TRUE(run_hex("a2 01 02 03 04"));
    ASSERT_EQ(s_pp.line_count, 4);
    ASSERT_STR_EQ(s_pp.lines[1].key, "1");
    ASSERT_STR_EQ(s_pp.lines[1].sep, ": ");
    ASSERT_STR_EQ(s_pp.lines[1].val, "2");
    ASSERT_STR_EQ(s_pp.lines[1].dot_path, "1");
    ASSERT_STR_EQ(s_pp.lines[1].trail, ",");
    ASSERT_STR_EQ(s_pp.lines[2].key, "3");
    ASSERT_STR_EQ(s_pp.lines[2].trail, "");

    ASSERT_TRUE(run_hex("a2 61 61 01 61 62 82 02 03"));
    ASSERT_EQ(s_pp.line_count, 7);
    ASSERT_STR_EQ(s_pp.lines[1].key, "a");
    ASSERT_STR_EQ(s_pp.lines[1].val, "1");
    ASSERT_STR_EQ(s_pp.lines[1].dot_path, "a");
    ASSERT_STR_EQ(s_pp.lines[2].key, "b");
    ASSERT_STR_EQ(s_pp.lines[2].val, "[");
    ASSERT_STR_EQ(s_pp.lines[3].dot_path, "b.0");
    ASSERT_EQ(s_pp.lines[3].depth, 2);
    ASSERT_STR_EQ(s_pp.lines[4].dot_path, "b.1");
    ASSERT_STR_EQ(s_pp.lines[5].val, "]");
    ASSERT_EQ(s_pp.lines[5].depth, 1);
    ASSERT_STR_EQ(s_pp.lines[6].val, "}");
    ASSERT_EQ(s_pp.lines[6].depth, 0);

    ASSERT_TRUE(run_hex("82 61 61 a1 61 62 61 63"));
    ASSERT_EQ(s_pp.line_count, 6);
    ASSERT_STR_EQ(s_pp.lines[1].val, "\"a\"");
    ASSERT_STR_EQ(s_pp.lines[3].key, "b");
    ASSERT_STR_EQ(s_pp.lines[3].val, "\"c\"");
    ASSERT_STR_EQ(s_pp.lines[3].dot_path, "1.b");

    ASSERT_TRUE(run_hex("a5 61 61 61 41 61 62 61 42 61 63 61 43 61 64 61 44 61 65 61 45"));
    ASSERT_EQ(s_pp.line_count, 7);
    ASSERT_STR_EQ(s_pp.lines[5].key, "e");
    ASSERT_STR_EQ(s_pp.lines[5].val, "\"E\"");

    ASSERT_TRUE(run_hex("c1 a1 61 61 01"));
    ASSERT_EQ(s_pp.line_count, 3);
    ASSERT_STR_EQ(s_pp.lines[0].val, "1({");
    ASSERT_STR_EQ(s_pp.lines[2].val, "})");
}

TEST(indefinite_lengths) {
    ASSERT_SCALAR("5f 42 01 02 43 03 04 05 ff", "h'0102030405'");
    ASSERT_SCALAR("7f 65 73 74 72 65 61 64 6d 69 6e 67 ff", "\"streaming\"");

    ASSERT_TRUE(run_hex("9f ff"));
    ASSERT_EQ(s_pp.line_count, 2);
    ASSERT_STR_EQ(s_pp.lines[0].val, "[_");
    ASSERT_STR_EQ(s_pp.lines[1].val, "]");

    ASSERT_TRUE(run_hex("9f 01 82 02 03 9f 04 05 ff ff"));
    ASSERT_EQ(s_pp.line_count, 11);
    ASSERT_STR_EQ(s_pp.lines[1].val, "1");
    ASSERT_STR_EQ(s_pp.lines[1].trail, ",");
    ASSERT_STR_EQ(s_pp.lines[5].trail, ",");
    ASSERT_STR_EQ(s_pp.lines[6].val, "[_");
    ASSERT_STR_EQ(s_pp.lines[8].trail, "");
    ASSERT_STR_EQ(s_pp.lines[9].val, "]");
    ASSERT_STR_EQ(s_pp.lines[9].trail, "");
    ASSERT_STR_EQ(s_pp.lines[10].val, "]");

    ASSERT_TRUE(run_hex("83 01 9f 02 03 ff 82 04 05"));
    ASSERT_EQ(s_pp.line_count, 11);
    ASSERT_STR_EQ(s_pp.lines[2].val, "[_");
    ASSERT_STR_EQ(s_pp.lines[5].trail, ",");

    ASSERT_TRUE(run_hex("9f 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 10 11 12 13 14 15 16 17 18 18 18 19 ff"));
    ASSERT_EQ(s_pp.line_count, 27);
    ASSERT_STR_EQ(s_pp.lines[25].val, "25");
    ASSERT_STR_EQ(s_pp.lines[25].dot_path, "24");
    ASSERT_TRUE(run_hex("98 19 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 10 11 12 13 14 15 16 17 18 18 18 19"));
    ASSERT_EQ(s_pp.line_count, 27);

    ASSERT_TRUE(run_hex("bf 61 61 01 61 62 9f 02 03 ff ff"));
    ASSERT_EQ(s_pp.line_count, 7);
    ASSERT_STR_EQ(s_pp.lines[0].val, "{_");
    ASSERT_STR_EQ(s_pp.lines[2].key, "b");
    ASSERT_STR_EQ(s_pp.lines[2].val, "[_");

    ASSERT_TRUE(run_hex("bf 63 46 75 6e f5 63 41 6d 74 21 ff"));
    ASSERT_EQ(s_pp.line_count, 4);
    ASSERT_STR_EQ(s_pp.lines[1].key, "Fun");
    ASSERT_STR_EQ(s_pp.lines[1].val, "true");
    ASSERT_STR_EQ(s_pp.lines[2].key, "Amt");
    ASSERT_STR_EQ(s_pp.lines[2].val, "-2");
    ASSERT_TRUE(s_pp.lines[2].is_numeric);
}

TEST(rejects_malformed_truncated_and_trailing) {
    ASSERT_FALSE(cbor_pp_run(&s_pp, s_buf, 0));
    ASSERT_EQ(s_pp.line_count, 0);
    ASSERT_FALSE(run_hex("18"));
    ASSERT_FALSE(run_hex("1c"));
    ASSERT_FALSE(run_hex("1f"));
    ASSERT_FALSE(run_hex("ff"));
    ASSERT_FALSE(run_hex("01 02"));
    ASSERT_FALSE(run_hex("81"));
    ASSERT_FALSE(run_hex("a1 01"));
    ASSERT_FALSE(run_hex("9f 01"));
    ASSERT_FALSE(run_hex("5f 01 ff"));
    ASSERT_FALSE(run_hex("5f 5f 41 01 ff ff"));
    ASSERT_FALSE(run_hex("f8 10"));
    ASSERT_FALSE(run_hex("7b ff ff ff ff ff ff ff ff"));
    ASSERT_FALSE(run_hex("a1 80 01"));
    ASSERT_FALSE(run_hex("a1 c1 01 02"));
    ASSERT_EQ(s_pp.line_count, 0);

    static const char json[] = "{\"a\":1}";
    ASSERT_FALSE(cbor_pp_run(&s_pp, (const uint8_t*)json, sizeof(json) - 1));
    static const char text[] = "hello";
    ASSERT_FALSE(cbor_pp_run(&s_pp, (const uint8_t*)text, sizeof(text) - 1));
}

TEST(depth_and_tag_nesting_limits) {
    for (size_t i = 0; i < 65; i++) s_buf[i] = 0x81;
    s_buf[65] = 0x00;
    ASSERT_FALSE(cbor_pp_run(&s_pp, s_buf, 66));

    ASSERT_FALSE(run_hex("c1 c1 c1 c1 c1 c1 c1 c1 c1 00"));
    ASSERT_SCALAR("c1 c1 c1 c1 c1 c1 c1 c1 00", "1(1(1(1(1(1(1(1(0))))))))");
}

TEST(truncates_oversized_strings_and_keys) {
    s_len = build_string(3, 400);
    ASSERT_TRUE(cbor_pp_run(&s_pp, s_buf, s_len));
    ASSERT_EQ(s_pp.line_count, 1);
    size_t vlen = strlen(s_pp.lines[0].val);
    ASSERT_EQ(vlen, (size_t)(JSON_PP_VAL_LEN - 1));
    ASSERT_EQ(s_pp.lines[0].val[0], '"');
    ASSERT_EQ(s_pp.lines[0].val[vlen - 1], '"');
    ASSERT_EQ(s_pp.lines[0].val_kind, JSON_PP_VAL_STRING);


    s_len = build_string(2, 400);
    ASSERT_TRUE(cbor_pp_run(&s_pp, s_buf, s_len));
    ASSERT_EQ(s_pp.line_count, 1);
    vlen = strlen(s_pp.lines[0].val);
    ASSERT_EQ(vlen, (size_t)(JSON_PP_VAL_LEN - 1));
    ASSERT_EQ(strncmp(s_pp.lines[0].val, "h'", 2), 0);
    ASSERT_EQ(s_pp.lines[0].val[vlen - 1], '\'');


    size_t p = 0;
    s_buf[p++] = 0xa1;
    s_buf[p++] = (uint8_t)((3 << 5) | 24);
    s_buf[p++] = 200;
    for (size_t i = 0; i < 200; i++) s_buf[p++] = (uint8_t)('a' + (i % 26));
    s_buf[p++] = 0x01;
    s_len = p;
    ASSERT_TRUE(cbor_pp_run(&s_pp, s_buf, s_len));
    ASSERT_EQ(s_pp.line_count, 3);
    ASSERT_EQ(strlen(s_pp.lines[1].key), (size_t)(JSON_PP_KEY_LEN - 1));
    ASSERT_STR_EQ(s_pp.lines[1].dot_path, s_pp.lines[1].key);
    ASSERT_STR_EQ(s_pp.lines[1].val, "1");
}

TEST(chart_lookup_through_json_pp_helpers) {
    ASSERT_TRUE(run_hex("a1 65 73 70 65 65 64 0a"));
    double v = 0;
    ASSERT_TRUE(json_pp_number_at(&s_pp, "speed", &v));
    ASSERT_EQ(v, 10.0);
    ASSERT_FALSE(json_pp_number_at(&s_pp, "rpm", &v));
    const JsonPPLine* l = json_pp_find(&s_pp, "speed");
    ASSERT_NOT_NULL(l);
    ASSERT_TRUE(l->is_numeric);
    ASSERT_TRUE(run_hex("f9 3e 00"));
    ASSERT_TRUE(json_pp_number_at(&s_pp, "anything", &v));
    ASSERT_EQ(v, 1.5);
}

TEST(cached_run_reuses_lines_until_key_changes) {
    static const uint8_t src[] = {0xa1, 0x61, 'a', 0x01};
    ASSERT_TRUE(cbor_pp_run_cached(&s_pp, src, sizeof(src), 7));
    ASSERT_EQ(s_pp.line_count, 3);
    s_pp.lines[1].val[0] = 'X';
    ASSERT_TRUE(cbor_pp_run_cached(&s_pp, src, sizeof(src), 7));
    ASSERT_EQ(s_pp.lines[1].val[0], 'X');
    ASSERT_TRUE(cbor_pp_run_cached(&s_pp, src, sizeof(src), 8));
    ASSERT_STR_EQ(s_pp.lines[1].val, "1");
    static const uint8_t bad[] = {0xff};
    ASSERT_FALSE(cbor_pp_run_cached(&s_pp, bad, sizeof(bad), 1));
    ASSERT_FALSE(cbor_pp_run_cached(&s_pp, bad, sizeof(bad), 1));
    ASSERT_EQ(s_pp.line_count, 0);
}

int main(void) {
    printf("cbor_pp tests:\n");
    RUN(appendix_a_integers);
    RUN(appendix_a_floats);
    RUN(appendix_a_simple_values_and_tags);
    RUN(appendix_a_strings);
    RUN(appendix_a_arrays_and_maps);
    RUN(indefinite_lengths);
    RUN(rejects_malformed_truncated_and_trailing);
    RUN(depth_and_tag_nesting_limits);
    RUN(truncates_oversized_strings_and_keys);
    RUN(chart_lookup_through_json_pp_helpers);
    RUN(cached_run_reuses_lines_until_key_changes);
    printf("All cbor_pp tests passed\n");
    return 0;
}
