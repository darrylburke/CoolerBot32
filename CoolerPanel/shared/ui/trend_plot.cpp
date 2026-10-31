#include "trend_plot.h"
#include "history.h"
#include "theme.h"
#include <cstdio>

// One column per 4 px; the widest plot (408 px) is 102 columns.
#define MAX_COLS 128

static lv_obj_t* overlay_label(lv_obj_t* parent, lv_color_t ink) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_color(l, ink, 0);
    // It sits over the plot: a backing keeps it readable across the trace.
    lv_obj_set_style_bg_color(l, th_page(), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_80, 0);
    lv_obj_set_style_pad_hor(l, 4, 0);
    lv_obj_set_style_radius(l, 4, 0);
    lv_label_set_text(l, "");
    return l;
}

bool trend_plot_create(TrendPlot& p, lv_obj_t* parent, int x, int y, int w, int h) {
    p.w = w; p.h = h;
    lv_color_t* buf = (lv_color_t*)lv_malloc(
        LV_CANVAS_BUF_SIZE(w, h, 16, LV_DRAW_BUF_STRIDE_ALIGN));
    if (buf) {
        p.canvas = lv_canvas_create(parent);
        lv_canvas_set_buffer(p.canvas, buf, w, h, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_pos(p.canvas, x, y);
    }

    // Axis ticks, coloured to their series so no legend is needed. The left
    // (temperature) scale is neutral, not cyan: cyan means "cooling was
    // requested", and an axis is not a series.
    for (int i = 0; i < 3; i++) {
        const int ty = y - 7 + i * ((h - 1) / 2);
        p.ax_t[i] = lv_label_create(parent);
        lv_obj_set_pos(p.ax_t[i], x - 32, ty);
        lv_obj_set_style_text_color(p.ax_t[i], th_ink2(), 0);
        lv_label_set_text(p.ax_t[i], "");
        p.ax_h[i] = lv_label_create(parent);
        lv_obj_set_pos(p.ax_h[i], x + w + 6, ty);
        lv_obj_set_style_text_color(p.ax_h[i], th_rh(), 0);
        lv_label_set_text(p.ax_h[i], "");
    }

    p.avg_lbl = overlay_label(parent, th_ink());
    p.rh_avg_lbl = overlay_label(parent, th_rh_avg());
    return p.canvas != nullptr;
}

static void labels_clear(TrendPlot& p) {
    for (int i = 0; i < 3; i++) { lv_label_set_text(p.ax_t[i], ""); lv_label_set_text(p.ax_h[i], ""); }
    lv_label_set_text(p.avg_lbl, "");
    lv_label_set_text(p.rh_avg_lbl, "");
    lv_obj_add_flag(p.avg_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(p.rh_avg_lbl, LV_OBJ_FLAG_HIDDEN);
}

static void fill(lv_layer_t* layer, lv_color_t c, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = LV_OPA_COVER;
    lv_area_t a = {x0, y0, x1, y1};
    lv_draw_rect(layer, &d, &a);
}

// 8 on, 6 off, 2 px thick: the app's dashed threshold line.
static void dashed(lv_layer_t* layer, lv_color_t c, int32_t y, int32_t w, int32_t h) {
    if (y < 0 || y > h - 1) return;
    int32_t y0 = y > 0 ? y - 1 : y;
    for (int32_t x = 0; x < w; x += 14) {
        const int32_t x1 = x + 7 < w ? x + 7 : w - 1;
        fill(layer, c, x, y0, x1, y0 + 1);
    }
}

// A rolling mean: a thin line joined column to column, broken where the record
// cannot support a whole hour.
static void mean_line(lv_layer_t* layer, lv_color_t c, const float* avg, const bool* has, int ncols,
                      int32_t colw, int32_t h, float lo, float hi) {
    auto yv = [&](float v) -> int32_t { return (int32_t)(h - 1 - ((v - lo) / (hi - lo)) * (h - 1)); };
    for (int i = 0; i < ncols; i++) {
        if (!has[i]) continue;
        float lo_v = avg[i], hi_v = avg[i];
        if (i > 0 && has[i - 1]) {
            if (avg[i - 1] < lo_v) lo_v = avg[i - 1];
            if (avg[i - 1] > hi_v) hi_v = avg[i - 1];
        }
        int32_t y0 = yv(hi_v), y1 = yv(lo_v);
        if (y0 < 0) y0 = 0;
        if (y1 > h - 1) y1 = h - 1;
        if (y1 - y0 < 2) y0 = y1 - 2;
        fill(layer, c, i * colw, y0, i * colw + colw - 1, y1);
    }
}

void trend_plot_draw(TrendPlot& p, const History& hist, int64_t now_epoch, const TrendPlotOpts& o) {
    if (!p.canvas) return;
    const int W = p.w, H = p.h;
    int ncols = W / 4;
    if (ncols > MAX_COLS) ncols = MAX_COLS;
    const SeriesShown& sh = o.shown;

    lv_layer_t layer;
    lv_canvas_init_layer(p.canvas, &layer);
    lv_canvas_fill_bg(p.canvas, th_surface(), LV_OPA_COVER);
    if (hist.size() < 2) { labels_clear(p); lv_canvas_finish_layer(p.canvas, &layer); return; }

    const int64_t to = chart_window_end(hist.newest_epoch(), now_epoch);
    const int64_t from = to - o.zoom_s;
    static Column cols[MAX_COLS];
    chart_downsample(hist, from, to, cols, (size_t)ncols);

    // Auto-range on the data, always including the band and the thresholds.
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < ncols; i++) {
        if (!cols[i].has) continue;
        if (cols[i].tmin < lo) lo = cols[i].tmin;
        if (cols[i].tmax > hi) hi = cols[i].tmax;
    }
    if (lo > hi) { labels_clear(p); lv_canvas_finish_layer(p.canvas, &layer); return; }
    if (o.band) {
        if (o.band_lo < lo) lo = o.band_lo;
        if (o.band_hi > hi) hi = o.band_hi;
    }
    for (size_t k = 0; k < o.n_t; k++) {
        if (o.t_lines[k] < lo) lo = o.t_lines[k];
        if (o.t_lines[k] > hi) hi = o.t_lines[k];
    }
    if (hi - lo < 1.0f) { hi = lo + 1.0f; }
    const float pad = (hi - lo) * 0.1f;
    lo -= pad; hi += pad;
    auto y_of = [&](float v) -> int32_t {
        return (int32_t)(H - 1 - ((v - lo) / (hi - lo)) * (H - 1));
    };

    // The rolling means, before the ranges: the humidity one widens its scale.
    static float avg[MAX_COLS], rh_avg[MAX_COLS];
    static bool has_avg[MAX_COLS], has_rh_avg[MAX_COLS];
    if (sh.avg)    chart_rolling_avg(hist, from, to, (size_t)ncols, avg, has_avg, AvgOf::Temp);
    if (sh.rh_avg) chart_rolling_avg(hist, from, to, (size_t)ncols, rh_avg, has_rh_avg, AvgOf::Rh);

    // Humidity gets its OWN vertical range. Temp sits near 4 and RH near 84,
    // so a shared axis would squash the temperature trace into a flat line
    // along the bottom and tell you nothing. Independent scales mean the two
    // traces are each readable; neither trace's absolute height is comparable
    // to the other's -- only its shape.
    float hlo = 1e9f, hhi = -1e9f;
    for (int i = 0; i < ncols; i++) {
        if (!cols[i].has) continue;
        if (cols[i].hmin < hlo) hlo = cols[i].hmin;
        if (cols[i].hmax > hhi) hhi = cols[i].hmax;
    }
    const bool any_rh = hlo <= hhi;
    if (any_rh && sh.rh_avg)
        for (int i = 0; i < ncols; i++) {
            if (!has_rh_avg[i]) continue;
            if (rh_avg[i] < hlo) hlo = rh_avg[i];
            if (rh_avg[i] > hhi) hhi = rh_avg[i];
        }
    if (any_rh)
        for (size_t k = 0; k < o.n_h; k++) {
            if (o.h_lines[k] < hlo) hlo = o.h_lines[k];
            if (o.h_lines[k] > hhi) hhi = o.h_lines[k];
        }
    // The humidity scale is there for the trace, its average, or both.
    const bool rh_scale = (sh.rh || sh.rh_avg) && any_rh;
    if (rh_scale) {
        if (hhi - hlo < 2.0f) { hhi = hlo + 2.0f; }
        const float hpad = (hhi - hlo) * 0.1f;
        hlo -= hpad; hhi += hpad;
    }
    auto hy_of = [&](float v) -> int32_t {
        return (int32_t)(H - 1 - ((v - hlo) / (hhi - hlo)) * (H - 1));
    };

    // Tick values, top to bottom, now that both ranges are settled. The
    // average line is drawn on the temperature scale too.
    const bool t_scale = sh.temp || sh.avg;
    // Whole degrees, unless the range is so narrow (a steady box without a
    // set-point band to widen it) that they would read "4 4 3".
    const char* tfmt = (hi - lo) < 4.0f ? "%.1f" : "%.0f";
    char tk[16];
    for (int i = 0; i < 3; i++) {
        snprintf(tk, sizeof(tk), tfmt, (double)(hi - (hi - lo) * i / 2.0f));
        lv_label_set_text(p.ax_t[i], t_scale ? tk : "");
        if (rh_scale) {
            snprintf(tk, sizeof(tk), "%.0f", (double)(hhi - (hhi - hlo) * i / 2.0f));
            lv_label_set_text(p.ax_h[i], tk);
        } else {
            lv_label_set_text(p.ax_h[i], "");
        }
    }

    const int32_t colw = W / ncols;

    // Relay 1 running: the whole column tinted, behind everything (the app
    // shades its chart the same way, with the temperature shown).
    if (o.shade_relay && sh.temp)
        for (int i = 0; i < ncols; i++)
            if (cols[i].has && cols[i].ac) fill(&layer, th_run_shade(), i * colw, 0, i * colw + colw - 1, H - 1);

    // Setpoint band -- the temperature's, so it goes with the temperature.
    if (o.band && sh.temp) {
        lv_draw_rect_dsc_t band;
        lv_draw_rect_dsc_init(&band);
        band.bg_color = lv_color_hex(0x212a26);
        band.bg_opa = LV_OPA_60;
        lv_area_t ba = {0, y_of(o.band_hi), W - 1, y_of(o.band_lo)};
        lv_draw_rect(&layer, &band, &ba);
    }

    // Temperature min/max bars. On the Trend page they are cyan where the
    // controller was requesting cooling for most of the column -- the same
    // cyan the status card wears while cooling, so the card is the legend.
    // Idle stretches are neutral grey rather than a dimmed accent: on a graph
    // the shape of the curve is the data, and it has to stay legible whether
    // or not anything was cooling.
    if (sh.temp)
        for (int i = 0; i < ncols; i++) {
            if (!cols[i].has) continue;      // gap stays blank
            const lv_color_t c = (!o.shade_relay && cols[i].ac) ? th_cool() : th_ink2();
            const Span sp = chart_joined_temp(cols, (size_t)i);   // meets its neighbour
            int32_t y0 = y_of(sp.hi);
            int32_t y1 = y_of(sp.lo);
            if (y1 - y0 < 2) y1 = y0 + 2;    // keep flat runs visible
            fill(&layer, c, i * colw, y0, i * colw + colw - 1, y1);
        }

    // Humidity: a thin trace rather than filled bars, so it reads as an
    // overlay on the temperature rather than competing with it.
    if (sh.rh && any_rh)
        for (int i = 0; i < ncols; i++) {
            if (!cols[i].has) continue;    // same honest gap as the temp trace
            const Span sp = chart_joined_rh(cols, (size_t)i);    // midpoint to midpoint
            int32_t y0 = hy_of(sp.hi);
            int32_t y1 = hy_of(sp.lo) + 2;
            if (y0 < 0) y0 = 0;
            if (y1 > H - 1) y1 = H - 1;
            if (y1 - y0 < 2) y0 = y1 - 2;
            fill(&layer, th_rh(), i * colw, y0, i * colw + colw - 1, y1);
        }

    // Thresholds, each on its own sensor's scale.
    if (t_scale)
        for (size_t k = 0; k < o.n_t; k++) dashed(&layer, th_warn(), y_of(o.t_lines[k]), W, H);
    if (rh_scale)
        for (size_t k = 0; k < o.n_h; k++) dashed(&layer, th_rh(), hy_of(o.h_lines[k]), W, H);

    if (sh.rh_avg && any_rh) mean_line(&layer, th_rh_avg(), rh_avg, has_rh_avg, ncols, colw, H, hlo, hhi);
    if (sh.avg)              mean_line(&layer, th_ink(), avg, has_avg, ncols, colw, H, lo, hi);

    // Window means, top right: temperature, then humidity under it.
    char abuf[24];
    float m;
    int row = 0;
    lv_obj_t* lbls[2] = {p.avg_lbl, p.rh_avg_lbl};
    const bool want[2] = {sh.avg, sh.rh_avg && any_rh};
    for (int k = 0; k < 2; k++) {
        const bool ok = want[k] && chart_window_mean(hist, from, to, m, k ? AvgOf::Rh : AvgOf::Temp);
        if (!ok) { lv_obj_add_flag(lbls[k], LV_OBJ_FLAG_HIDDEN); continue; }
        if (k) snprintf(abuf, sizeof(abuf), "avg %.0f%%", (double)m);
        else   snprintf(abuf, sizeof(abuf), "avg %.1f", (double)m);
        lv_label_set_text(lbls[k], abuf);
        lv_obj_remove_flag(lbls[k], LV_OBJ_FLAG_HIDDEN);
        lv_obj_update_layout(lbls[k]);   // its new width, for the right-alignment
        lv_obj_align_to(lbls[k], p.canvas, LV_ALIGN_TOP_RIGHT, -4, 4 + row * 22);
        row++;
    }
    lv_canvas_finish_layer(p.canvas, &layer);
}
