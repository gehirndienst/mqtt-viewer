// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef SUBSCRIPTION_H
#define SUBSCRIPTION_H

#include <stdbool.h>

/**
 * @brief Test whether an MQTT topic string matches an MQTT topic filter.
 *
 * Implements MQTT 3.1.1 §4.7 wildcard semantics:
 * - `+` matches exactly one topic level.
 * - `#` matches the remaining levels (must be the last character).
 * - `$SYS` topics are not matched by a bare `#` or `+/...` filter.
 *
 * @param filter  MQTT topic filter (may contain `+` or `#`).
 * @param topic   Concrete topic string to test (no wildcards).
 * @return true when @p topic matches @p filter.
 */
bool mqtt_topic_matches(const char* filter, const char* topic);

#endif
