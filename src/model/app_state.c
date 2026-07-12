// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <string.h>

#include "core/mqtt_client.h"
#include "model/app_state.h"

void app_state_init(AppState* state) {
    memset(state, 0, sizeof(AppState));
    topic_tree_init(&state->topic_tree, TOPIC_TREE_CAPACITY);
    message_buf_init(&state->global_history, HISTORY_CAPACITY);
    connection_log_init(&state->conn_log, CONN_LOG_CAPACITY);
    spsc_queue_init(&state->msg_queue, MSG_QUEUE_CAPACITY, sizeof(MqttMessage));
    state->conn_state = CONN_DISCONNECTED;
    state->log_panel_open = true;
    state->tree_width_ratio = 0.4f;
    state->active_profile_idx = -1;
    state->profile_active_field = -1;
    state->profile_last_idx = -2;
}

void app_state_destroy(AppState* state) {
    topic_tree_destroy(&state->topic_tree);
    message_buf_destroy(&state->global_history);
    connection_log_destroy(&state->conn_log);
    spsc_queue_destroy(&state->msg_queue);
}
