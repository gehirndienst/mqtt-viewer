// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>

#include "model/util.h"
#include "model/version.h"
#include "platform/log.h"
#include "platform/update_check.h"

// Fixed command: nothing user-controlled is ever interpolated into the shell line
#define UPDATE_CHECK_CMD \
    "curl -fsS -m 8 -H 'Accept: application/vnd.github+json' " \
    "https://api.github.com/repos/gehirndienst/mqtt-viewer/releases/latest 2>/dev/null"
#define UPDATE_REPLY_CAP 65536

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static UpdateResult s_result;
static char s_current[64];

static void set_result(UpdateStatus status, const char* latest, const char* error) {
    pthread_mutex_lock(&s_lock);
    s_result = (UpdateResult){.status = status};
    util_str_copy(s_result.latest, sizeof(s_result.latest), latest);
    util_str_copy(s_result.error, sizeof(s_result.error), error);
    pthread_mutex_unlock(&s_lock);
}

static void* fail(const char* why, const char* log_detail) {
    LOG_WARN("update check: %s", log_detail);
    set_result(UPDATE_FAILED, NULL, why);
    return NULL;
}

static void* check_worker(void* arg) {
    (void)arg;
    static char reply[UPDATE_REPLY_CAP];
    FILE* p = popen(UPDATE_CHECK_CMD, "r");
    if (!p) return fail("Could not run curl", "popen failed");

    reply[fread(reply, 1, sizeof(reply) - 1, p)] = '\0';
    if (pclose(p) != 0) return fail("Could not reach GitHub (is curl installed and are you online?)", "curl failed");

    char tag[64];
    if (!version_extract_tag(reply, tag, sizeof(tag))) return fail("Unexpected reply from GitHub", "no tag_name");
    set_result(version_is_newer(tag, s_current) ? UPDATE_AVAILABLE : UPDATE_UP_TO_DATE, tag, NULL);
    return NULL;
}

void update_check_start(const char* current_version) {
    pthread_mutex_lock(&s_lock);
    bool busy = s_result.status == UPDATE_CHECKING;
    if (!busy) s_result = (UpdateResult){.status = UPDATE_CHECKING};
    pthread_mutex_unlock(&s_lock);
    if (busy) return;

    util_str_copy(s_current, sizeof(s_current), current_version);
    pthread_t t;
    if (pthread_create(&t, NULL, check_worker, NULL) != 0) {
        set_result(UPDATE_FAILED, NULL, "Could not start update check");
        return;
    }
    pthread_detach(t);
}

UpdateResult update_check_poll(void) {
    pthread_mutex_lock(&s_lock);
    UpdateResult r = s_result;
    pthread_mutex_unlock(&s_lock);
    return r;
}
