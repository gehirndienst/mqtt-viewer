// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef PUBLISH_PANEL_H
#define PUBLISH_PANEL_H

#include "core/mqtt_client.h"
#include "model/app_state.h"

/**
 * @brief Render the floating publish panel (bottom-docked).
 *
 * Handles text input for topic/payload, QoS toggle, retain toggle, and the
 * Publish button. Closes the panel after a successful publish.
 * @param state  Application state (reads/writes publish_* fields).
 * @param mqtt   MQTT client used to dispatch the publish call.
 */
void publish_panel_render(AppState* state, MqttClient* mqtt);

#endif
