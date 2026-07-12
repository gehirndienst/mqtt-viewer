// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <time.h>

// Verbosity scale: 0=off, 1=error, 2=warn, 3=info, 4=debug
#ifndef MQTT_VIEWER_LOG_LEVEL
#define MQTT_VIEWER_LOG_LEVEL 3
#endif

static inline const char* log_timestamp(void) {
    static char buf[24];
    time_t t = time(NULL);
    struct tm* tm_info = localtime(&t);
    strftime(buf, sizeof(buf), "%H:%M:%S", tm_info);
    return buf;
}

#define LOG_ERROR(fmt, ...) \
    do { \
        if (MQTT_VIEWER_LOG_LEVEL >= 1) \
            fprintf(stderr, "[%s ERROR] " fmt "\n", log_timestamp() __VA_OPT__(, ) __VA_ARGS__); \
    } while (0)

#define LOG_WARN(fmt, ...) \
    do { \
        if (MQTT_VIEWER_LOG_LEVEL >= 2) \
            fprintf(stderr, "[%s  WARN] " fmt "\n", log_timestamp() __VA_OPT__(, ) __VA_ARGS__); \
    } while (0)

#define LOG_INFO(fmt, ...) \
    do { \
        if (MQTT_VIEWER_LOG_LEVEL >= 3) \
            fprintf(stderr, "[%s  INFO] " fmt "\n", log_timestamp() __VA_OPT__(, ) __VA_ARGS__); \
    } while (0)

#define LOG_DEBUG(fmt, ...) \
    do { \
        if (MQTT_VIEWER_LOG_LEVEL >= 4) \
            fprintf(stderr, "[%s DEBUG] " fmt "\n", log_timestamp() __VA_OPT__(, ) __VA_ARGS__); \
    } while (0)

#endif
