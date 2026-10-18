// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
/**
 * @file
 * @brief Release version comparison (MAJOR.MINOR.PATCH with optional leading 'v' and -prerelease suffix)
 */
#ifndef VERSION_H
#define VERSION_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Test whether version @p latest is strictly newer than @p current.
 *
 * Accepts `1.2.3`, `v1.2.3`, `1.2` (missing parts are 0) and `1.2.3-rc1`; a pre-release sorts before the same
 * release. Build metadata after `+` is ignored.
 * @return true when @p latest > @p current; false when equal, older, or either string is NULL or unparseable.
 */
bool version_is_newer(const char* latest, const char* current);

/**
 * @brief Pull the `tag_name` string value out of a GitHub "latest release" JSON reply.
 * @param json  NUL-terminated reply body.
 * @param out   Receives the tag (NUL-terminated).
 * @param cap   Size of @p out.
 * @return true when a complete tag fitting in @p out was found.
 */
bool version_extract_tag(const char* json, char* out, size_t cap);

#endif
