// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef BROKER_PROFILE_H
#define BROKER_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_PROFILE_SUBS 16

typedef struct {
    char topic[256];
    uint8_t qos;
} ProfileSubscription;

typedef struct {
    int id;
    char name[128];
    char host[256];
    uint16_t port;
    int protocol_version; // 31, 311, or 5
    char client_id[128];
    bool clean_session;
    uint16_t keepalive_secs;
    char username[128];
    char password[128];
    int transport; // 0=TCP, 1=WS, 2=WSS
    char tls_ca_cert[512];
    char tls_client_cert[512];
    char tls_client_key[512];
    int tls_version; // 12 or 13
    bool tls_verify;
    ProfileSubscription subscriptions[MAX_PROFILE_SUBS];
    int subscription_count;
} BrokerProfile;

static_assert(MAX_PROFILE_SUBS <= 32, "MAX_PROFILE_SUBS must fit the profile dialog's render loop budget");
static_assert(sizeof(BrokerProfile) <= 8192, "BrokerProfile must fit comfortably on the stack");

/**
 * @brief Fill @p profile with sensible defaults.
 *
 * Sets port 1883, MQTT 3.1.1, clean session, 60 s keepalive, TLS verify on,
 * and default subscriptions # (QoS 1) and $SYS/# (QoS 0)
 * @param profile  Profile struct to overwrite.
 */
void broker_profile_init_default(BrokerProfile* profile);

#endif
