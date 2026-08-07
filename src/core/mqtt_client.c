// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <mosquitto.h>

#include "core/mqtt_client.h"
#include "platform/log.h"

#define MQTT_CLIENT_MAX_SUBS 32
static_assert(MQTT_CLIENT_MAX_SUBS <= 128, "MQTT_CLIENT_MAX_SUBS: increase sub_topics array if you raise this");

typedef enum {
    MQTT_CS_DISCONNECTED,
    MQTT_CS_CONNECTING,
    MQTT_CS_CONNECTED,
} MqttConnState;

struct MqttClient {
    struct mosquitto* mosq;
    SpscQueue* msg_queue; // pushes MqttMessage to main thread
    ConnectionLog* log;
    _Atomic MqttConnState conn_state;
    atomic_int last_disconnect_rc;
    pthread_mutex_t sub_mutex;
    char sub_topics[MQTT_CLIENT_MAX_SUBS][256];
    uint8_t sub_qos[MQTT_CLIENT_MAX_SUBS];
    uint32_t sub_count;
};

static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}

static void on_connect(struct mosquitto* mosq, void* obj, int rc) {
    MqttClient* client = obj;
    if (rc == 0) {
        atomic_store(&client->conn_state, MQTT_CS_CONNECTED);
        atomic_store(&client->last_disconnect_rc, 0);
        connection_log_add(client->log, CONN_LOG_INFO, "Connected to broker");
        pthread_mutex_lock(&client->sub_mutex);
        for (uint32_t i = 0; i < client->sub_count; i++) {
            mosquitto_subscribe(mosq, NULL, client->sub_topics[i], client->sub_qos[i]);
        }
        pthread_mutex_unlock(&client->sub_mutex);
    } else {
        atomic_store(&client->conn_state, MQTT_CS_DISCONNECTED);
        char buf[256];
        snprintf(buf, sizeof(buf), "Connection failed: %s", mosquitto_connack_string(rc));
        connection_log_add(client->log, CONN_LOG_ERROR, buf);
    }
}

static void on_disconnect(struct mosquitto* mosq, void* obj, int rc) {
    (void)mosq;
    MqttClient* client = obj;
    atomic_store(&client->conn_state, MQTT_CS_DISCONNECTED);
    atomic_store(&client->last_disconnect_rc, rc);
    if (rc == 0) {
        connection_log_add(client->log, CONN_LOG_INFO, "Disconnected cleanly");
    } else {
        char buf[256];
        const char* mosq_msg = mosquitto_strerror(rc);
        const char* sys_msg = strerror(rc);
        if (mosq_msg && mosq_msg[0] && strcmp(mosq_msg, "Unknown error") != 0) {
            snprintf(buf, sizeof(buf), "Connection lost: %s (rc=%d)", mosq_msg, rc);
        } else {
            snprintf(buf, sizeof(buf), "Connection lost: %s (rc=%d)", sys_msg, rc);
        }
        connection_log_add(client->log, CONN_LOG_WARN, buf);
    }
}

static void on_message(struct mosquitto* mosq, void* obj, const struct mosquitto_message* msg) {
    (void)mosq;
    MqttClient* client = obj;

    MqttMessage m = {
        .timestamp_us = now_us(),
        .payload_len = (uint32_t)msg->payloadlen,
        .qos = (uint8_t)msg->qos,
        .retained = msg->retain,
    };
    strncpy(m.topic, msg->topic, sizeof(m.topic) - 1);
    m.topic[sizeof(m.topic) - 1] = '\0';

    // allocate payload copy (main thread will own it via arena later)
    if (msg->payloadlen > 0) {
        m.payload = malloc((size_t)msg->payloadlen);
        if (!m.payload) return;
        memcpy(m.payload, msg->payload, (size_t)msg->payloadlen);
    } else {
        m.payload = NULL;
    }

    if (!spsc_queue_push(client->msg_queue, &m)) {
        free(m.payload);
    }
}

static void on_subscribe(struct mosquitto* mosq, void* obj, int mid, int qos_count, const int* granted_qos) {
    (void)mosq;
    (void)mid;
    (void)qos_count;
    (void)granted_qos;
    MqttClient* client = obj;
    connection_log_add(client->log, CONN_LOG_INFO, "Subscription acknowledged");
}

MqttClient* mqtt_client_new(SpscQueue* msg_queue, ConnectionLog* log) {
    mosquitto_lib_init();
    MqttClient* client = calloc(1, sizeof(MqttClient));
    if (!client) {
        LOG_ERROR("mqtt_client_new: out of memory");
        mosquitto_lib_cleanup();
        return NULL;
    }
    client->msg_queue = msg_queue;
    client->log = log;
    atomic_store(&client->conn_state, MQTT_CS_DISCONNECTED);
    atomic_store(&client->last_disconnect_rc, 0);
    pthread_mutex_init(&client->sub_mutex, NULL);
    client->sub_count = 0;
    return client;
}

static void stop_network_thread(struct mosquitto* mosq) {
    if (mosquitto_disconnect(mosq) == MOSQ_ERR_SUCCESS) {
        mosquitto_loop_stop(mosq, false);
    } else {
        mosquitto_loop_stop(mosq, true);
    }
}

void mqtt_client_destroy(MqttClient* client) {
    if (!client) return;
    if (client->mosq) {
        stop_network_thread(client->mosq);
        mosquitto_destroy(client->mosq);
    }
    pthread_mutex_destroy(&client->sub_mutex);
    mosquitto_lib_cleanup();
    free(client);
}

bool mqtt_client_connect(MqttClient* client, const MqttConnectOpts* opts) {
    if (client->mosq) {
        stop_network_thread(client->mosq);
        mosquitto_destroy(client->mosq);
    }

    atomic_store(&client->conn_state, MQTT_CS_CONNECTING);
    atomic_store(&client->last_disconnect_rc, 0);
    pthread_mutex_lock(&client->sub_mutex);
    client->sub_count = 0;
    pthread_mutex_unlock(&client->sub_mutex);

    // MQTT persistent sessions require a non-empty client_id
    static char generated_id[32];
    const char* effective_id = opts->client_id;
    if (!opts->clean_session && (!opts->client_id || opts->client_id[0] == '\0')) {
        struct timespec ts2;
        clock_gettime(CLOCK_REALTIME, &ts2);
        snprintf(generated_id, sizeof(generated_id), "mqttviewer-%08lx", (unsigned long)(ts2.tv_sec ^ ts2.tv_nsec));
        effective_id = generated_id;
    }

    client->mosq = mosquitto_new(effective_id, opts->clean_session, client);
    if (!client->mosq) {
        char buf[256];
        snprintf(buf, sizeof(buf), "Failed to create MQTT client (errno %d: %s)", errno, strerror(errno));
        LOG_ERROR("mqtt_client_connect: mosquitto_new() failed - %s", buf);
        connection_log_add(client->log, CONN_LOG_ERROR, buf);
        return false;
    }

    mosquitto_reconnect_delay_set(client->mosq, 1, 30, true);

    int version;
    if (opts->protocol_version == 5) {
        version = MQTT_PROTOCOL_V5;
    } else if (opts->protocol_version == 31) {
        version = MQTT_PROTOCOL_V31;
    } else {
        version = MQTT_PROTOCOL_V311; // default: 3.1.1
    }
    mosquitto_int_option(client->mosq, MOSQ_OPT_PROTOCOL_VERSION, version);

    // TCP default 0; WS=1
    if (opts->transport == 1) {
#if LIBMOSQUITTO_VERSION_NUMBER >= 2001000
        mosquitto_int_option(client->mosq, MOSQ_OPT_TRANSPORT, MOSQ_T_WEBSOCKETS);
#else
        connection_log_add(client->log, CONN_LOG_WARN,
                           "WebSocket transport requires libmosquitto >= 2.1 - using TCP instead");
#endif
    }

    // auth
    if (opts->username) {
        mosquitto_username_pw_set(client->mosq, opts->username, opts->password);
    }

    // tls
    if (opts->tls_version != 0) {
        const char* ca_cert = (opts->tls_ca_cert && opts->tls_ca_cert[0]) ? opts->tls_ca_cert : NULL;
        if (!ca_cert) {
            static const char* const sys_ca_paths[] = {
                "/etc/ssl/cert.pem", // macOS, Alpine
                "/etc/ssl/certs/ca-certificates.crt", // Debian/Ubuntu
                "/etc/pki/tls/certs/ca-bundle.crt", // RHEL/Fedora
                NULL,
            };
            for (int k = 0; sys_ca_paths[k]; k++) {
                if (access(sys_ca_paths[k], R_OK) == 0) {
                    ca_cert = sys_ca_paths[k];
                    break;
                }
            }
            if (ca_cert) {
                char log_buf[320];
                snprintf(log_buf, sizeof(log_buf), "TLS: no CA cert specified, using %s", ca_cert);
                connection_log_add(client->log, CONN_LOG_INFO, log_buf);
            }
        }

        const char* client_cert = (opts->tls_client_cert && opts->tls_client_cert[0]) ? opts->tls_client_cert : NULL;
        const char* client_key = (opts->tls_client_key && opts->tls_client_key[0]) ? opts->tls_client_key : NULL;
        int rc_tls = mosquitto_tls_set(client->mosq, ca_cert, NULL, client_cert, client_key, NULL);
        if (rc_tls != MOSQ_ERR_SUCCESS) {
            char buf2[256];
            snprintf(buf2, sizeof(buf2), "TLS setup failed: %s", mosquitto_strerror(rc_tls));
            LOG_WARN("mqtt_client_connect: %s", buf2);
            connection_log_add(client->log, CONN_LOG_WARN, buf2);
        }

        const char* tls_ver_str = NULL;
        if (opts->tls_version == 12)
            tls_ver_str = "tlsv1.2";
        else if (opts->tls_version == 13)
            tls_ver_str = "tlsv1.3";

        mosquitto_tls_opts_set(client->mosq, opts->tls_verify ? 1 : 0, tls_ver_str, NULL);

        if (!opts->tls_verify) {
            mosquitto_tls_insecure_set(client->mosq, true);
        }
    }

    mosquitto_connect_callback_set(client->mosq, on_connect);
    mosquitto_disconnect_callback_set(client->mosq, on_disconnect);
    mosquitto_message_callback_set(client->mosq, on_message);
    mosquitto_subscribe_callback_set(client->mosq, on_subscribe);

    char buf[512];
    snprintf(buf, sizeof(buf), "Connecting to %s:%d...", opts->host, opts->port);
    connection_log_add(client->log, CONN_LOG_INFO, buf);

    int rc = mosquitto_connect_async(client->mosq, opts->host, opts->port, opts->keepalive_secs);
    if (rc != MOSQ_ERR_SUCCESS) {
        snprintf(buf, sizeof(buf), "Connect failed: %s", mosquitto_strerror(rc));
        connection_log_add(client->log, CONN_LOG_ERROR, buf);
        return false;
    }

    rc = mosquitto_loop_start(client->mosq);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_ERROR("mqtt_client_connect: mosquitto_loop_start() failed: %s", mosquitto_strerror(rc));
        connection_log_add(client->log, CONN_LOG_ERROR, "Failed to start MQTT loop thread");
        mosquitto_destroy(client->mosq);
        client->mosq = NULL;
        return false;
    }

    return true;
}

void mqtt_client_disconnect(MqttClient* client) {
    if (client->mosq) {
        MqttConnState cs = atomic_load(&client->conn_state);
        bool was_active = (cs == MQTT_CS_CONNECTING || cs == MQTT_CS_CONNECTED);
        stop_network_thread(client->mosq);
        mosquitto_destroy(client->mosq);
        client->mosq = NULL;
        atomic_store(&client->conn_state, MQTT_CS_DISCONNECTED);
        atomic_store(&client->last_disconnect_rc, 0);
        if (was_active) {
            connection_log_add(client->log, CONN_LOG_INFO, "Disconnected");
        }
    }
}

bool mqtt_client_is_connected(MqttClient* client) {
    return atomic_load(&client->conn_state) == MQTT_CS_CONNECTED;
}

bool mqtt_client_is_connecting(MqttClient* client) {
    return atomic_load(&client->conn_state) == MQTT_CS_CONNECTING;
}

int mqtt_client_last_disconnect_rc(MqttClient* client) {
    return atomic_load(&client->last_disconnect_rc);
}

bool mqtt_client_subscribe(MqttClient* client, const MqttSubscribeOpts* opts) {
    if (!client->mosq) return false;

    // store unconditionally so on_connect can re-send if called before CONNACK
    pthread_mutex_lock(&client->sub_mutex);
    if (client->sub_count < MQTT_CLIENT_MAX_SUBS) {
        strncpy(client->sub_topics[client->sub_count], opts->topic_filter, sizeof(client->sub_topics[0]) - 1);
        client->sub_topics[client->sub_count][sizeof(client->sub_topics[0]) - 1] = '\0';
        client->sub_qos[client->sub_count] = opts->qos;
        client->sub_count++;
    }
    pthread_mutex_unlock(&client->sub_mutex);

    if (atomic_load(&client->conn_state) == MQTT_CS_CONNECTED) {
        int rc = mosquitto_subscribe(client->mosq, NULL, opts->topic_filter, opts->qos);
        if (rc == MOSQ_ERR_SUCCESS) {
            char buf[512];
            snprintf(buf, sizeof(buf), "Subscribed to %s (QoS %d)", opts->topic_filter, opts->qos);
            connection_log_add(client->log, CONN_LOG_INFO, buf);
        }
    }

    return true;
}

bool mqtt_client_unsubscribe(MqttClient* client, const char* topic_filter) {
    if (!client->mosq) return false;
    bool ok = mosquitto_unsubscribe(client->mosq, NULL, topic_filter) == MOSQ_ERR_SUCCESS;
    if (ok) {
        // remove from stored list to prevent re-subscription on reconnect
        pthread_mutex_lock(&client->sub_mutex);
        for (uint32_t i = 0; i < client->sub_count; i++) {
            if (strcmp(client->sub_topics[i], topic_filter) == 0) {
                uint32_t last = client->sub_count - 1;
                if (i != last) {
                    memcpy(client->sub_topics[i], client->sub_topics[last], sizeof(client->sub_topics[0]));
                    client->sub_qos[i] = client->sub_qos[last];
                }
                client->sub_count--;
                break;
            }
        }
        pthread_mutex_unlock(&client->sub_mutex);
    }

    return ok;
}

bool mqtt_client_publish(MqttClient* client, const char* topic, const void* payload, uint32_t payload_len, uint8_t qos,
                         bool retain) {
    if (!client->mosq) return false;
    return mosquitto_publish(client->mosq, NULL, topic, (int)payload_len, payload, qos, retain) == MOSQ_ERR_SUCCESS;
}
