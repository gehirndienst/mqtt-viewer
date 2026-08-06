// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "model/broker_profile.h"
#include "model/chart_series.h"
#include "model/connection_log.h"
#include "model/message_buf.h"
#include "model/spsc_queue.h"
#include "model/topic_tree.h"

#define MAX_SUBSCRIPTIONS 32
#define TOPIC_TREE_CAPACITY 4096
#define HISTORY_CAPACITY 32768
#define CONN_LOG_CAPACITY 5000
#define MSG_QUEUE_CAPACITY 4096

typedef enum {
    CONN_DISCONNECTED,
    CONN_CONNECTING,
    CONN_CONNECTED,
    CONN_RECONNECTING,
    CONN_ERROR,
} ConnState;

typedef enum {
    SUB_STATE_ACTIVE,
    SUB_STATE_PAUSED,
} SubState;

typedef struct {
    char topic_filter[256];
    uint8_t qos;
    SubState state;
    uint64_t message_count;
    uint64_t bytes_total;
    float throughput;
    uint64_t window_timestamps[128]; // sliding window for throughput calc
    uint32_t window_head;
    uint32_t window_count;
} Subscription;

typedef struct {
    // Model
    TopicTree topic_tree;
    MessageBuf global_history;
    ConnectionLog conn_log;

    // Connection
    ConnState conn_state;
    char conn_error[256];
    float ping_rtt_ms;
    char broker_host[256];
    uint16_t broker_port;

    // Active subscriptions
    Subscription subscriptions[MAX_SUBSCRIPTIONS];
    int subscription_count;

    // MQTT communication queue (MQTT I/O thread -> main thread)
    SpscQueue msg_queue;

    // UI state
    TopicNode* selected_topic;
    bool publish_panel_open;
    bool log_panel_open;
    bool profile_dialog_open;
    char topic_filter[256];
    bool topic_filter_focused;
    float tree_width_ratio;
    bool diff_enabled;

    // Context menu state
    bool disconnect_requested; // handled in main.c
    bool context_menu_open;
    TopicNode* context_menu_target;
    float context_menu_x;
    float context_menu_y;

    // CSV export state
    bool export_requested; // set by the inspector's CSV button; handled in main.c
    char export_topic[256]; // subtree filter; "" = export all
    char export_path[1024]; // destination path (auto-resolved)

    // Publish panel state
    char publish_topic[256];
    char publish_payload[4096];
    uint8_t publish_qos;
    bool publish_retain;
    int publish_active_field; // keyboard focus: 0 = topic, 1 = payload

    // Inspector state
    int inspector_view; // active ViewMode tab (VIEW_JSON/TEXT/HEX/HISTORY)

    // Profiles
    BrokerProfile profiles[32];
    int profile_count;
    int active_profile_idx; // -1 if none

    // Profile dialog edit session
    int profile_active_field; // focused field index, -1 = none
    int profile_last_idx; // tracks active_profile_idx to resync edit buffers; -2 = never seen
    float profile_save_flash_timer; // >0 while the Save-confirmation flash is showing

    // Stats
    uint32_t total_topics;
    uint64_t total_messages;
    float msgs_per_sec;

    // Numeric time-series plots
    ChartSeries chart_series[CHART_MAX_SERIES];
} AppState;

/**
 * @brief Allocate and zero-initialize all sub-structures of @p state.
 *
 * Initializes the topic tree, message buffer, connection log, SPSC queue,
 * and arena. Does not open the database or connect to any broker.
 * @param state  AppState to initialize (caller-allocated).
 */
void app_state_init(AppState* state);

/**
 * @brief Destroy all sub-structures of @p state and free heap memory.
 *
 * Must be called after the MQTT client is disconnected and the SQLite
 * database is closed. After this call @p state must not be used.
 */
void app_state_destroy(AppState* state);

#endif
