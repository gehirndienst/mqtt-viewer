// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef CSV_EXPORT_H
#define CSV_EXPORT_H

#include <stdint.h>
#include <stdio.h>

#include "model/message_buf.h"

/**
 * @brief Write message records as RFC 4180 CSV to @p out.
 *
 * Columns: timestamp,topic,qos,retained,payload
 * Rows are oldest-first. Payload bytes are written as-is (RFC 4180 quoted
 * when needed); NUL bytes become spaces. Lines end with CRLF
 *
 * @param buf           Source ring buffer
 * @param topic_prefix  Only export records whose topic equals this prefix or
 *                      lives under it (whole-segment match: "a/b" matches
 *                      "a/b" and "a/b/c" but not "a/bc"). NULL or "" = all
 * @param out           Destination stream
 * @return Number of data rows written, or -1 on write error.
 */
int64_t csv_export_write(const MessageBuf* buf, const char* topic_prefix, FILE* out);

#endif
