// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "platform/export_path.h"

static bool dir_exists(const char* path) {
    struct stat st;
    return path != NULL && path[0] != '\0' && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool export_path_resolve(const char* topic, char* out, size_t out_size) {
    char dir[1024];
    const char* override = getenv("MQTT_VIEWER_CSV_EXPORT_PATH");
    if (override != NULL && override[0] != '\0') {
        snprintf(dir, sizeof(dir), "%s", override);
    } else {
        const char* home = getenv("HOME");
        if (home == NULL || home[0] == '\0') return false;
        snprintf(dir, sizeof(dir), "%s/Downloads", home);
        if (!dir_exists(dir)) {
            const char* xdg = getenv("XDG_DOWNLOAD_DIR");
            if (dir_exists(xdg)) {
                snprintf(dir, sizeof(dir), "%s", xdg);
            } else {
                snprintf(dir, sizeof(dir), "%s", home);
            }
        }
    }

    char slug[128] = "all";
    if (topic != NULL && topic[0] != '\0') {
        size_t n = 0;
        for (const char* p = topic; *p != '\0' && n < sizeof(slug) - 1; p++) {
            slug[n++] = (*p == '/') ? '-' : *p;
        }
        slug[n] = '\0';
    }

    time_t now = time(NULL);
    struct tm tm_local;
    localtime_r(&now, &tm_local);
    char stamp[20];
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm_local);

    int written = snprintf(out, out_size, "%s/mqtt-export-%s-%s.csv", dir, slug, stamp);
    return written > 0 && (size_t)written < out_size;
}
