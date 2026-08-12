// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include "model/connection_log.h"
#include "model/spsc_queue.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char* host;
    uint16_t port;
    const char* client_id; // NULL for auto-generated
    bool clean_session;
    uint16_t keepalive_secs;
    const char* username; // NULL if no auth
    const char* password; // NULL if no auth
    int protocol_version; // 31=MQTT 3.1, 311=MQTT 3.1.1, 5=MQTT 5
    int transport; // 0=TCP (default), 1=WS (WebSocket)
    // TLS - enabled when tls_version != 0 (WSS also implies TLS)
    const char* tls_ca_cert; // path to CA certificate file
    const char* tls_client_cert; // path to client cert (NULL = no mutual auth)
    const char* tls_client_key; // path to client key (NULL = no mutual auth)
    int tls_version; // 12 = TLSv1.2, 13 = TLSv1.3 (0 = default/any)
    bool tls_verify; // true = verify server cert (default)
    // SSH tunnel - when enabled, an `ssh -L` process is spawned to forward a local port to
    // host:port and that local port is dialed instead
    bool ssh_tunnel_enabled;
    const char* ssh_jump_host;
    uint16_t ssh_jump_port; // 0 => default 22
    const char* ssh_jump_user; // NULL => rely on ssh_config / current OS user
    const char* ssh_jump_key_path; // NULL => default identity / ssh-agent
    const char* ssh_jump_password; // NULL/empty => key-based auth; non-empty => password auth via sshpass
} MqttConnectOpts;

typedef struct {
    const char* topic_filter;
    uint8_t qos;
} MqttSubscribeOpts;

typedef struct {
    uint64_t timestamp_us;
    char topic[256];
    uint8_t* payload;
    uint32_t payload_len;
    uint8_t qos;
    bool retained;
} MqttMessage;

typedef struct MqttClient MqttClient;

/**
 * @brief Allocate a new MqttClient.
 * @param msg_queue  SPSC queue the client pushes received MqttMessage values into.
 * @param log        Connection log for user-visible events (connect, errors, etc.).
 * @return New client handle, or NULL on OOM.
 */
MqttClient* mqtt_client_new(SpscQueue* msg_queue, ConnectionLog* log);

/** @brief Disconnect (if connected), stop the I/O thread, and free all resources. */
void mqtt_client_destroy(MqttClient* client);

/**
 * @brief Connect to a broker and start the background I/O thread.
 *
 * Creates a new mosquitto instance, applies TLS if configured, and calls
 * mosquitto_connect() + mosquitto_loop_start(). Automatic exponential-backoff
 * reconnect is enabled (1 s initial, 30 s max).
 * @param client  Client handle.
 * @param opts    Connection parameters (host, port, auth, TLS, etc.).
 * @return true on success; error is logged to @p client->log and stderr.
 */
bool mqtt_client_connect(MqttClient* client, const MqttConnectOpts* opts);

/** @brief Initiate a clean disconnect. The I/O thread keeps running. */
void mqtt_client_disconnect(MqttClient* client);

/**
 * @brief Check whether an active SSH tunnel is still alive (main thread only, call once per frame)
 */
void mqtt_client_poll_ssh_tunnel(MqttClient* client);

/** @brief Return true when the broker handshake has completed successfully. */
bool mqtt_client_is_connected(MqttClient* client);

/** @brief Return true while a connect attempt is in progress (between mqtt_client_connect()
 *         and the first on_connect / on_disconnect callback). */
bool mqtt_client_is_connecting(MqttClient* client);

/**
 * @brief Return the rc from the last on_disconnect callback.
 * @return 0 when never disconnected or after a clean disconnect;
 *         non-zero (MQTT reason code) when the connection was lost unexpectedly.
 *         A non-zero value persists until the next successful reconnect.
 */
int mqtt_client_last_disconnect_rc(MqttClient* client);

/**
 * @brief Subscribe to a topic filter (main thread only).
 *
 * Stores the subscription internally so it can be re-applied automatically
 * after an unintended disconnect.
 * @return true when mosquitto_subscribe() accepted the request.
 */
bool mqtt_client_subscribe(MqttClient* client, const MqttSubscribeOpts* opts);

/**
 * @brief Unsubscribe from a topic filter and remove it from the stored list.
 * @return true when mosquitto_unsubscribe() accepted the request.
 */
bool mqtt_client_unsubscribe(MqttClient* client, const char* topic_filter);

/**
 * @brief Publish a message (main thread only).
 * @param topic        NUL-terminated topic string.
 * @param payload      Message bytes (may be NULL for zero-length payload).
 * @param payload_len  Byte length of @p payload.
 * @param qos          QoS level (0, 1, or 2).
 * @param retain       Set the retained flag on the broker.
 * @return true when the publish was accepted by the I/O thread.
 */
bool mqtt_client_publish(MqttClient* client, const char* topic, const void* payload, uint32_t payload_len, uint8_t qos,
                         bool retain);

#endif
