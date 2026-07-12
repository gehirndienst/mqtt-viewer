// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef PROFILE_DIALOG_H
#define PROFILE_DIALOG_H

#include "core/mqtt_client.h"
#include "model/app_state.h"
#include "platform/db.h"

/**
 * @brief Render the broker profile editor dialog (floating, full-height).
 *
 * Allows creating, editing, and deleting broker profiles. Saves changes
 * to the database immediately. Connects to the selected profile on "Connect".
 * @param state  Application state.
 * @param db     Database handle used to persist profile changes.
 * @param mqtt   MQTT client used to initiate the connection.
 */
void profile_dialog_render(AppState* state, Db* db, MqttClient* mqtt);

#endif
