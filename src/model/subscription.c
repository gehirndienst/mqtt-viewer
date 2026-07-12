// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <string.h>

#include "model/subscription.h"

bool mqtt_topic_matches(const char* filter, const char* topic) {
    if (!filter || !topic) return false;

    // $ topics don't match wildcard root levels
    bool topic_starts_dollar = (topic[0] == '$');

    const char* f = filter;
    const char* t = topic;

    while (*f != '\0' && *t != '\0') {
        if (*f == '#') {
            // # must be at the end of filter segment
            if (topic_starts_dollar && f == filter) return false;
            return true; // matches everything remaining
        }
        if (*f == '+') {
            if (topic_starts_dollar && f == filter) return false;
            // Skip one topic level in t
            while (*t != '\0' && *t != '/') t++;
            f++; // skip '+' in filter
            // Both should be at '/' or '\0' now
            if (*f == '/' && *t == '/') {
                f++;
                t++;
            } else if (*f == '\0' && *t == '\0') {
                return true;
            } else {
                return false;
            }
            continue;
        }
        if (*f != *t) return false;
        f++;
        t++;
    }

    // Handle trailing '#': "a/#" should match "a" (topic exhausted, filter has "/#" left)
    if (*t == '\0' && *f == '/' && *(f + 1) == '#' && *(f + 2) == '\0') return true;
    // Handle plain trailing '#'
    if (*f == '#') return true;
    // Both exhausted
    return (*f == '\0' && *t == '\0');
}
