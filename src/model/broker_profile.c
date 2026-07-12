// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <string.h>

#include "model/broker_profile.h"

void broker_profile_init_default(BrokerProfile* profile) {
    memset(profile, 0, sizeof(BrokerProfile));
    profile->port = 1883;
    profile->protocol_version = 311;
    profile->clean_session = true;
    profile->keepalive_secs = 60;
    profile->tls_version = 0;
    profile->tls_verify = true;
    profile->subscriptions[0].topic[0] = '#';
    profile->subscriptions[0].topic[1] = '\0';
    profile->subscriptions[0].qos = 1;
    strncpy(profile->subscriptions[1].topic, "$SYS/#", sizeof(profile->subscriptions[1].topic) - 1);
    profile->subscriptions[1].topic[sizeof(profile->subscriptions[1].topic) - 1] = '\0';
    profile->subscriptions[1].qos = 0;
    profile->subscription_count = 2;
}
