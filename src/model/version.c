// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "model/util.h"
#include "model/version.h"

typedef struct {
    unsigned long part[3];
    bool prerelease;
} Version;

static bool version_parse(const char* s, Version* out) {
    if (!s) return false;
    if (*s == 'v' || *s == 'V') s++;
    *out = (Version){0};
    for (int i = 0; i < 3; i++) {
        if (*s < '0' || *s > '9') return false;
        char* end;
        out->part[i] = strtoul(s, &end, 10);
        s = end;
        if (*s != '.') break;
        s++;
    }
    if (*s == '-')
        out->prerelease = true;
    else if (*s != '\0' && *s != '+')
        return false;
    return true;
}

bool version_is_newer(const char* latest, const char* current) {
    Version a, b;
    if (!version_parse(latest, &a) || !version_parse(current, &b)) return false;
    for (int i = 0; i < 3; i++) {
        if (a.part[i] != b.part[i]) return a.part[i] > b.part[i];
    }
    return !a.prerelease && b.prerelease;
}

bool version_extract_tag(const char* json, char* out, size_t cap) {
    static const char key[] = "\"tag_name\"";
    const char* p = json ? strstr(json, key) : NULL;
    char tag[64];
    int consumed = 0; // stays 0 unless the closing quote was matched, which rejects an unterminated or oversized tag
    if (!p || !out || sscanf(p + sizeof(key) - 1, " : \"%63[^\"]\"%n", tag, &consumed) != 1 || consumed == 0) {
        return false;
    }
    if (strlen(tag) >= cap) return false;
    util_str_copy(out, cap, tag);
    return true;
}
