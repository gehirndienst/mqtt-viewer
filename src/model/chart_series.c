// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include "model/chart_series.h"

#include <math.h>
#include <string.h>

void chart_series_reset(ChartSeries* s) {
    memset(s, 0, sizeof(*s));
}

void chart_series_init(ChartSeries* s, const char* topic, const char* dot_path) {
    chart_series_reset(s);
    s->active = true;

    if (topic) {
        size_t n = strlen(topic);
        if (n >= sizeof(s->topic)) n = sizeof(s->topic) - 1;
        memcpy(s->topic, topic, n);
        s->topic[n] = '\0';
    }

    if (dot_path) {
        size_t n = strlen(dot_path);
        if (n >= sizeof(s->dot_path)) n = sizeof(s->dot_path) - 1;
        memcpy(s->dot_path, dot_path, n);
        s->dot_path[n] = '\0';
    }
}

bool chart_series_push_sample(ChartSeries* s, uint64_t ts_us, double value) {
    if (!s->active) return false;
    if (!isfinite(value)) return false;

    if (s->count > 0) {
        if (ts_us <= s->last_sample_ts_us) return false;
        if (ts_us - s->last_sample_ts_us < CHART_MIN_SAMPLE_INTERVAL_US) return false;
    }

    while (s->count > 0) {
        const ChartSample* oldest = &s->samples[s->head];
        if (ts_us - oldest->ts_us <= CHART_TIME_WINDOW_US) break;
        if (oldest->value == s->y_min || oldest->value == s->y_max) s->y_dirty = true;
        s->head = (s->head + 1) % CHART_MAX_SAMPLES;
        s->count--;
    }

    uint32_t write_idx;
    if (s->count < CHART_MAX_SAMPLES) {
        write_idx = (s->head + s->count) % CHART_MAX_SAMPLES;
        s->count++;
    } else {
        const ChartSample* evicted = &s->samples[s->head];
        if (evicted->value == s->y_min || evicted->value == s->y_max) {
            s->y_dirty = true;
        }
        write_idx = s->head;
        s->head = (s->head + 1) % CHART_MAX_SAMPLES;
    }
    s->samples[write_idx].ts_us = ts_us;
    s->samples[write_idx].value = value;
    s->last_sample_ts_us = ts_us;

    if (s->count == 1) {
        s->y_min = value;
        s->y_max = value;
        s->y_dirty = false;
    } else {
        if (value < s->y_min) s->y_min = value;
        if (value > s->y_max) s->y_max = value;
    }
    return true;
}

void chart_series_recompute_minmax(ChartSeries* s) {
    if (s->count == 0) {
        s->y_min = 0;
        s->y_max = 0;
        s->y_dirty = false;
        return;
    }

    double mn = s->samples[s->head].value;
    double mx = mn;
    for (uint32_t i = 1; i < s->count; i++) {
        uint32_t idx = (s->head + i) % CHART_MAX_SAMPLES;
        double v = s->samples[idx].value;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    s->y_min = mn;
    s->y_max = mx;
    s->y_dirty = false;
}

const ChartSample* chart_series_get(const ChartSeries* s, uint32_t i) {
    if (i >= s->count) return NULL;
    uint32_t idx = (s->head + i) % CHART_MAX_SAMPLES;
    return &s->samples[idx];
}
