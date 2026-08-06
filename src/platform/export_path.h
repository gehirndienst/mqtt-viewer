// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef EXPORT_PATH_H
#define EXPORT_PATH_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Build the default CSV export destination path.
 *
 * Directory priority: $MQTT_VIEWER_CSV_EXPORT_PATH if set (used verbatim),
 * else $HOME/Downloads if it exists, else $XDG_DOWNLOAD_DIR if it exists,
 * else $HOME.
 * Filename: mqtt-export-<slug>-<YYYYMMDD-HHMMSS>.csv
 * where <slug> is @p topic with '/' replaced by '-', or "all" when @p topic
 * is NULL or empty
 *
 * @param topic     Topic path used for the file-name slug; NULL or "" = all.
 * @param out       Destination buffer for the absolute path.
 * @param out_size  Size of @p out in bytes.
 * @return true on success; false if no home directory or @p out is too small.
 */
bool export_path_resolve(const char* topic, char* out, size_t out_size);

#endif
