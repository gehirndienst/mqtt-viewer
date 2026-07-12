// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include "model/connection_log.h"
#include "test_helpers.h"

TEST(create_and_destroy) {
    ConnectionLog log;
    connection_log_init(&log, 16);
    ASSERT_EQ(connection_log_count(&log), 0);
    connection_log_destroy(&log);
}

TEST(add_entry) {
    ConnectionLog log;
    connection_log_init(&log, 16);
    connection_log_add(&log, CONN_LOG_INFO, "Connected to broker");
    ASSERT_EQ(connection_log_count(&log), 1);
    LogEntry e;
    ASSERT_EQ(connection_log_get(&log, 0, &e), true);
    ASSERT_EQ(e.level, CONN_LOG_INFO);
    ASSERT_STR_EQ(e.message, "Connected to broker");
    connection_log_destroy(&log);
}

TEST(eviction) {
    ConnectionLog log;
    connection_log_init(&log, 4);
    connection_log_add(&log, CONN_LOG_INFO, "msg0");
    connection_log_add(&log, CONN_LOG_INFO, "msg1");
    connection_log_add(&log, CONN_LOG_INFO, "msg2");
    connection_log_add(&log, CONN_LOG_INFO, "msg3");
    connection_log_add(&log, CONN_LOG_WARN, "msg4");
    ASSERT_EQ(connection_log_count(&log), 4);
    LogEntry oldest;
    ASSERT_EQ(connection_log_get(&log, 0, &oldest), true);
    ASSERT_STR_EQ(oldest.message, "msg1");
    connection_log_destroy(&log);
}

TEST(clear) {
    ConnectionLog log;
    connection_log_init(&log, 16);
    connection_log_add(&log, CONN_LOG_INFO, "test");
    connection_log_clear(&log);
    ASSERT_EQ(connection_log_count(&log), 0);
    connection_log_destroy(&log);
}

int main(void) {
    printf("connection_log tests:\n");
    RUN(create_and_destroy);
    RUN(add_entry);
    RUN(eviction);
    RUN(clear);
    printf("All connection_log tests passed\n");
    return 0;
}
