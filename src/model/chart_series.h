// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef CHART_SERIES_H
#define CHART_SERIES_H

#include <stdbool.h>
#include <stdint.h>

// 2-minute rolling window at 20 Hz worst case -> 2400 samples
// Round up for headroom; series memory is ~CHART_MAX_SAMPLES 16 B per slot
#define CHART_MAX_SAMPLES 2560
#define CHART_MAX_SERIES 16
#define CHART_DOT_PATH_LEN 128
#define CHART_TOPIC_LEN 256
// Minimum interval between samples for a single series (microseconds)
// Decimates very high-rate topics. 50 ms = 20 Hz cap per series
#define CHART_MIN_SAMPLE_INTERVAL_US 50000ULL
// Sliding time window per series: samples older than this from the newest are evicted from the head when a new sample
// arrives, so the X-axis represents the last CHART_TIME_WINDOW_US of activity
#define CHART_TIME_WINDOW_US 120000000ULL

typedef struct {
    uint64_t ts_us;
    double value;
} ChartSample;

typedef struct {
    bool active;
    char topic[CHART_TOPIC_LEN]; // full topic path
    char dot_path[CHART_DOT_PATH_LEN]; // empty string- whole payload as number
    ChartSample samples[CHART_MAX_SAMPLES];
    uint32_t head; // ring buffer head (oldest index)
    uint32_t count; // number of valid samples (<= CHART_MAX_SAMPLES)
    double y_min; // running min over current window
    double y_max; // running max over current window
    bool y_dirty; // true if min/max may need recompute (eviction of extremum)
    uint64_t last_sample_ts_us; // for sample-interval throttle and dedupe
} ChartSeries;

/** @brief Reset all fields to inactive/empty */
void chart_series_reset(ChartSeries* s);

/**
 * @brief Activate a new series for (@p topic, @p dot_path).
 * @param s         Series slot to activate.
 * @param topic     Full MQTT topic path.
 * @param dot_path  JSON dot path to extract; empty string = whole payload as number.
 */
void chart_series_init(ChartSeries* s, const char* topic, const char* dot_path);

/**
 * @brief Push a (timestamp, value) sample, subject to the per-series rate throttle.
 * @param s       Series to update.
 * @param ts_us   Sample timestamp in microseconds.
 * @param value   Numeric value.
 * @return true if the sample was kept; false if throttled or duplicate.
 */
bool chart_series_push_sample(ChartSeries* s, uint64_t ts_us, double value);

/**
 * @brief Recompute y_min / y_max by scanning the current ring buffer.
 *
 * Clears the y_dirty flag. Call when an eviction may have removed an extremum.
 */
void chart_series_recompute_minmax(ChartSeries* s);

/**
 * @brief Read the i-th sample (0 = oldest) from the ring buffer.
 * @return Pointer to the sample, or NULL if @p i >= count.
 */
const ChartSample* chart_series_get(const ChartSeries* s, uint32_t i);

#endif
