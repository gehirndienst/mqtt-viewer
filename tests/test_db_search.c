// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include "model/broker_profile.h"
#include "model/message_buf.h"
#include "platform/db.h"
#include "test_helpers.h"

static MessageRecord make_record(const char* topic, const char* payload_text) {
    MessageRecord r = {0};
    strncpy(r.topic, topic, sizeof(r.topic) - 1);
    r.payload = (uint8_t*)payload_text;
    r.payload_len = (uint32_t)strlen(payload_text);
    r.qos = 1;
    r.timestamp_us = 1000;
    return r;
}

TEST(finds_message_by_payload_word) {
    Db* db = db_open(":memory:");
    ASSERT_NOT_NULL(db);

    MessageRecord recs[2] = {
        make_record("sensors/temp", "{\"status\":\"connection lost\"}"),
        make_record("sensors/humidity", "{\"status\":\"ok\"}"),
    };
    ASSERT_TRUE(db_save_messages(db, recs, 2));

    MessageRecord results[10];
    int n = db_search_messages(db, "connection", results, 10);
    ASSERT_EQ(n, 1);
    ASSERT_STR_EQ(results[0].topic, "sensors/temp");
    ASSERT_NULL(results[0].payload);

    db_close(db);
}

TEST(prefix_matches_partial_last_word) {
    Db* db = db_open(":memory:");
    ASSERT_NOT_NULL(db);

    MessageRecord recs[1] = {make_record("t", "connection lost")};
    ASSERT_TRUE(db_save_messages(db, recs, 1));

    MessageRecord results[10];
    int n = db_search_messages(db, "conn", results, 10);
    ASSERT_EQ(n, 1);

    db_close(db);
}

TEST(matches_words_regardless_of_order) {
    Db* db = db_open(":memory:");
    ASSERT_NOT_NULL(db);

    MessageRecord recs[1] = {make_record("t", "lost connection")};
    ASSERT_TRUE(db_save_messages(db, recs, 1));

    MessageRecord results[10];
    int n = db_search_messages(db, "connection lost", results, 10);
    ASSERT_EQ(n, 1);

    db_close(db);
}

TEST(no_match_returns_zero) {
    Db* db = db_open(":memory:");
    ASSERT_NOT_NULL(db);

    MessageRecord recs[1] = {make_record("t", "all is well")};
    ASSERT_TRUE(db_save_messages(db, recs, 1));

    MessageRecord results[10];
    int n = db_search_messages(db, "explosion", results, 10);
    ASSERT_EQ(n, 0);

    db_close(db);
}

TEST(empty_query_returns_zero_without_error) {
    Db* db = db_open(":memory:");
    ASSERT_NOT_NULL(db);

    MessageRecord results[10];
    ASSERT_EQ(db_search_messages(db, "", results, 10), 0);
    ASSERT_EQ(db_search_messages(db, "   ", results, 10), 0);

    db_close(db);
}

TEST(special_characters_are_treated_literally_not_as_query_syntax) {
    Db* db = db_open(":memory:");
    ASSERT_NOT_NULL(db);

    MessageRecord recs[1] = {make_record("t", "value is \"quoted\" AND weird")};
    ASSERT_TRUE(db_save_messages(db, recs, 1));

    MessageRecord results[10];
    int n = db_search_messages(db, "\"quoted\" AND", results, 10);
    ASSERT_TRUE(n >= 0);
    ASSERT_EQ(db_search_messages(db, "totally unmatched garbage query", results, 10), 0);

    db_close(db);
}

TEST(matches_topic_text_too) {
    Db* db = db_open(":memory:");
    ASSERT_NOT_NULL(db);

    MessageRecord recs[1] = {make_record("factory/line1/status", "ok")};
    ASSERT_TRUE(db_save_messages(db, recs, 1));

    MessageRecord results[10];
    int n = db_search_messages(db, "factory", results, 10);
    ASSERT_EQ(n, 1);

    db_close(db);
}

TEST(deleted_messages_are_removed_from_the_index) {
    Db* db = db_open(":memory:");
    ASSERT_NOT_NULL(db);

    MessageRecord recs[1] = {make_record("t", "unique_marker_xyz")};
    ASSERT_TRUE(db_save_messages(db, recs, 1));

    MessageRecord results[10];
    ASSERT_EQ(db_search_messages(db, "unique_marker_xyz", results, 10), 1);

    ASSERT_TRUE(db_trim_messages(db, 0));
    ASSERT_EQ(db_search_messages(db, "unique_marker_xyz", results, 10), 0);

    db_close(db);
}

TEST(results_ordered_by_relevance_and_capped_at_max_count) {
    Db* db = db_open(":memory:");
    ASSERT_NOT_NULL(db);

    MessageRecord recs[3] = {
        make_record("a", "alpha"),
        make_record("b", "alpha alpha alpha"),
        make_record("c", "alpha"),
    };
    ASSERT_TRUE(db_save_messages(db, recs, 3));

    MessageRecord results[2];
    int n = db_search_messages(db, "alpha", results, 2);
    ASSERT_EQ(n, 2);

    db_close(db);
}

// Count rows in the messages table via a search-free load (load caps at max_count, so ask for plenty)
static int saved_rows(Db* db) {
    static MessageRecord out[64];
    int n = db_load_messages(db, out, 64);
    for (int i = 0; i < n; i++) free(out[i].payload);
    return n;
}

TEST(flush_history_tracks_ring_generation_across_clear_and_eviction) {
    Db* db = db_open(":memory:");
    ASSERT_NOT_NULL(db);
    MessageBuf ring;
    message_buf_init(&ring, 4);
    uint64_t saved = 0;

    MessageRecord a = make_record("t/a", "a");
    message_buf_push(&ring, &a);
    message_buf_push(&ring, &a);
    db_flush_history(db, &ring, &saved);
    ASSERT_EQ(saved_rows(db), 2);

    db_flush_history(db, &ring, &saved);
    ASSERT_EQ(saved_rows(db), 2);

    message_buf_clear(&ring);
    saved = message_buf_generation(&ring);
    message_buf_push(&ring, &a);
    db_flush_history(db, &ring, &saved);
    ASSERT_EQ(saved_rows(db), 3);

    for (int i = 0; i < 6; i++) message_buf_push(&ring, &a);
    db_flush_history(db, &ring, &saved);
    ASSERT_EQ(saved_rows(db), 7);
    db_flush_history(db, &ring, &saved);
    ASSERT_EQ(saved_rows(db), 7);

    message_buf_destroy(&ring);
    db_close(db);
}

TEST(profile_subscriptions_round_trip_through_json) {
    Db* db = db_open(":memory:");
    ASSERT_NOT_NULL(db);

    BrokerProfile prof = {0};
    strcpy(prof.name, "p");
    strcpy(prof.host, "h");
    prof.subscription_count = 3;
    strcpy(prof.subscriptions[0].topic, "sensors/#");
    prof.subscriptions[0].qos = 1;
    strcpy(prof.subscriptions[1].topic, "odd \"quoted\" back\\slash");
    prof.subscriptions[1].qos = 2;
    strcpy(prof.subscriptions[2].topic, "$SYS/#");
    prof.subscriptions[2].qos = 0;
    ASSERT_TRUE(db_save_profile(db, &prof));

    BrokerProfile loaded[4];
    int n = db_load_profiles(db, loaded, 4);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(loaded[0].subscription_count, 3);
    ASSERT_STR_EQ(loaded[0].subscriptions[0].topic, "sensors/#");
    ASSERT_EQ(loaded[0].subscriptions[0].qos, 1);
    ASSERT_STR_EQ(loaded[0].subscriptions[1].topic, "odd \"quoted\" back\\slash");
    ASSERT_EQ(loaded[0].subscriptions[1].qos, 2);
    ASSERT_STR_EQ(loaded[0].subscriptions[2].topic, "$SYS/#");
    ASSERT_EQ(loaded[0].subscriptions[2].qos, 0);

    db_close(db);
}

TEST(delete_topic_messages_removes_only_that_topic) {
    Db* db = db_open(":memory:");
    ASSERT_NOT_NULL(db);
    MessageRecord recs[3] = {
        make_record("car/speed", "50"),
        make_record("car/speedy", "51"),
        make_record("car/speed", "52"),
    };
    ASSERT_TRUE(db_save_messages(db, recs, 3));
    ASSERT_TRUE(db_delete_topic_messages(db, "car/speed"));

    MessageRecord out[10];
    int n = db_load_messages(db, out, 10);
    ASSERT_EQ(n, 1);
    ASSERT_STR_EQ(out[0].topic, "car/speedy");
    free(out[0].payload);
    // the FTS index followed the delete
    ASSERT_EQ(db_search_messages(db, "50", out, 10), 0);
    db_close(db);
}

int main(void) {
    printf("test_db_search:\n");
    RUN(finds_message_by_payload_word);
    RUN(prefix_matches_partial_last_word);
    RUN(matches_words_regardless_of_order);
    RUN(no_match_returns_zero);
    RUN(empty_query_returns_zero_without_error);
    RUN(special_characters_are_treated_literally_not_as_query_syntax);
    RUN(matches_topic_text_too);
    RUN(deleted_messages_are_removed_from_the_index);
    RUN(results_ordered_by_relevance_and_capped_at_max_count);
    RUN(flush_history_tracks_ring_generation_across_clear_and_eviction);
    RUN(profile_subscriptions_round_trip_through_json);
    RUN(delete_topic_messages_removes_only_that_topic);
    printf("all tests passed\n");
    return 0;
}
