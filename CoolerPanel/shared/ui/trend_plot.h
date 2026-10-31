#pragma once
#include "lvgl.h"
#include "chart.h"
#include <stddef.h>
class History;

// The trend graph shared by the Trend page (the Cooler) and the Temp/Humidity
// page (the dehumidifier): temperature min/max bars on the left scale,
// humidity on its own right scale, the 1-hour rolling average of each, and
// the window means over the top right. Moved out of screen_trend.cpp so both
// pages draw the same way.

struct TrendPlotOpts {
    SeriesShown shown;
    int zoom_s = 10800;
    // A shaded temperature band behind the bars (the Cooler's set point).
    bool band = false;
    float band_lo = 0, band_hi = 0;
    // How a running relay shows: false colours the bar cyan (the Cooler's
    // cooling call), true tints the whole column (the dehumidifier's relay 1).
    bool shade_relay = false;
    // Dashed horizontal lines: temperature-scale (amber) and humidity-scale
    // (olive) thresholds, e.g. the dehumidifier's enabled rules.
    const float* t_lines = nullptr; size_t n_t = 0;
    const float* h_lines = nullptr; size_t n_h = 0;
};

struct TrendPlot {
    lv_obj_t* canvas = nullptr;
    lv_obj_t* ax_t[3] = {};
    lv_obj_t* ax_h[3] = {};
    lv_obj_t* avg_lbl = nullptr;     // "avg 3.9" over the plot's top right
    lv_obj_t* rh_avg_lbl = nullptr;  // "avg 84%" below it
    int w = 0, h = 0;
};

// Canvas of w x h at (x, y) on parent, with tick labels in the 36 px either
// side and the average readouts. w must be a multiple of 4 (one column per
// 4 px). False if the canvas buffer could not be allocated.
bool trend_plot_create(TrendPlot& p, lv_obj_t* parent, int x, int y, int w, int h);

void trend_plot_draw(TrendPlot& p, const History& hist, int64_t now_epoch, const TrendPlotOpts& o);
