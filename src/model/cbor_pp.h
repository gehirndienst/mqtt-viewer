// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef CBOR_PP_H
#define CBOR_PP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "model/json_pp.h"

/**
 * @brief Strict RFC 8949 decoder that renders one CBOR data item as diagnostic-notation lines into a JsonPP, so the
 *        inspector, chart lookup and diff code treat it exactly like pretty-printed JSON
 *
 * @return true if @p src is exactly one well-formed item that consumes all @p len bytes. On false pp->line_count is 0
 */
bool cbor_pp_run(JsonPP* pp, const uint8_t* src, size_t len);

/**
 * @brief Like cbor_pp_run(), but a no-op while (@p src, @p key) equal the previous call's. Never share one JsonPP
 *        between this and json_pp_run_cached(): both key on (src, key)
 * @return The decode result, cached or freshly computed - not what json_pp_run_cached() returns, which is whether the
 *         lines were rebuilt
 */
bool cbor_pp_run_cached(JsonPP* pp, const uint8_t* src, size_t len, uint64_t key);

#endif
