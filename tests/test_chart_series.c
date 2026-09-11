// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include "model/chart_series.h"
#include "test_helpers.h"

#define MS(x) ((uint64_t)(x) * 1000ULL)

TEST(rate_cap_holds) {
    ChartSeries s;
    chart_series_init(&s, "car/speed", "speed");
    for (int i = 0; i < 100; i++) chart_series_push_sample(&s, MS(10 * i), 50.0);
    ASSERT_EQ(s.count, 20);
    chart_series_reset(&s);
}

TEST(outlier_inside_bucket_replaces_kept_sample) {
    ChartSeries s;
    chart_series_init(&s, "car/speed", "speed");
    ASSERT_TRUE(chart_series_push_sample(&s, MS(0), 50.0));
    ASSERT_TRUE(chart_series_push_sample(&s, MS(50), 51.0));
    ASSERT_TRUE(chart_series_push_sample(&s, MS(60), 999.0));
    ASSERT_FALSE(chart_series_push_sample(&s, MS(70), 52.0));
    ASSERT_EQ(s.count, 2);
    ASSERT_EQ(chart_series_get(&s, 1)->value, 999.0);
    ASSERT_EQ(chart_series_get(&s, 1)->ts_us, MS(50));
    ASSERT_EQ(s.y_max, 999.0);
    ASSERT_EQ(s.y_min, 50.0);
    ASSERT_TRUE(chart_series_push_sample(&s, MS(100), 53.0));
    ASSERT_EQ(s.count, 3);
    chart_series_reset(&s);
}

TEST(replaced_extremum_marks_minmax_dirty) {
    ChartSeries s;
    chart_series_init(&s, "car/speed", "speed");
    chart_series_push_sample(&s, MS(0), 50.0);
    chart_series_push_sample(&s, MS(50), 999.0); // current max
    chart_series_push_sample(&s, MS(100), 60.0);
    ASSERT_TRUE(chart_series_push_sample(&s, MS(110), -5.0));
    ASSERT_EQ(s.y_min, -5.0);
    ASSERT_EQ(s.y_max, 999.0);
    chart_series_reset(&s);
}

TEST(first_bucket_keeps_spike_too) {
    ChartSeries s;
    chart_series_init(&s, "car/speed", "speed");
    ASSERT_TRUE(chart_series_push_sample(&s, MS(0), 50.0));
    ASSERT_TRUE(chart_series_push_sample(&s, MS(10), 999.0));
    ASSERT_FALSE(chart_series_push_sample(&s, MS(20), 51.0));
    ASSERT_EQ(s.count, 1);
    ASSERT_EQ(chart_series_get(&s, 0)->value, 999.0);
    chart_series_reset(&s);
}

TEST(out_of_order_and_nonfinite_dropped) {
    ChartSeries s;
    chart_series_init(&s, "car/speed", "speed");
    ASSERT_TRUE(chart_series_push_sample(&s, MS(100), 50.0));
    ASSERT_FALSE(chart_series_push_sample(&s, MS(100), 60.0));
    ASSERT_FALSE(chart_series_push_sample(&s, MS(90), 60.0));
    ASSERT_FALSE(chart_series_push_sample(&s, MS(200), 1.0 / 0.0));
    ASSERT_EQ(s.count, 1);
    chart_series_reset(&s);
}

int main(void) {
    printf("Running chart_series tests...\n");
    RUN(rate_cap_holds);
    RUN(outlier_inside_bucket_replaces_kept_sample);
    RUN(replaced_extremum_marks_minmax_dirty);
    RUN(first_bucket_keeps_spike_too);
    RUN(out_of_order_and_nonfinite_dropped);
    printf("All chart_series tests passed\n");
    return 0;
}
