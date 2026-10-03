#include "screen_trend.h"
#include "chart.h"
#include "history.h"
#include "cooler_state.h"
#include "app.h"
#include "theme.h"
#include "icons.h"
#include "panel_config.h"
#include "status_text.h"
#include "platform.h"
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// "Quiet Slate": a calm domestic display, not an instrument panel.
//
// The governing rule is that ONLY THE THING CURRENTLY HAPPENING IS COLOURED.
// Everything at rest stays in the greys. The previous layout coloured six
// cards at once, which is visually loud and, worse, makes the one card you
// need to notice indistinguishable from the five you don't.
//
// Asymmetric top: temperature is the reason you look at this panel, so it
// gets two thirds of the row; humidity and the coil (fin) temperature stack
// beside it -- the two readings that explain the box temperature.
//
// The second row is the one AC unit: what the controller is doing (the wide
// status card, which also names the mode) and what the compressor is actually
// doing (inferred from the fin slope). Keeping those two side by side is the
// point: "Cooling" beside "Starting" for long is the AC not answering.
//
// Everything sits on a three-column grid: 3 * 141 + 2 * 12 gutters + 2 * 16
// margins == 480, and the temperature block spans the first two columns.
//
//   8..30     header (name / age)
//   38..150   temp (cols 1-2) + humidity over coil (col 3)
//   158..204  status (cols 1-2) | compressor
//   214..382  graph
//   390..422  zoom
//   430..450  footer
// ---------------------------------------------------------------------------
#define PAD        16
#define GUT        12
#define COL_W      141
#define COL2_X     (PAD + COL_W + GUT)
#define COL3_X     (PAD + (COL_W + GUT) * 2)
#define HDR_Y      8
#define R1_Y       38
#define R1_H       112
#define BIG_W      (COL_W * 2 + GUT)
#define HUM_H      58
#define R2_Y       158
#define R2_H       46
#define COIL_Y     (R1_Y + HUM_H + 8)

#define GRAPH_X    36
#define GRAPH_Y    214
#define GRAPH_W    408
#define GRAPH_H    168
#define NCOLS      102

#define ZOOM_Y     390
#define FOOT_Y     430

static lv_obj_t* s_root;
static lv_obj_t* s_canvas;
static lv_obj_t* s_foot_lbl;
static lv_obj_t* s_age_lbl;
// Override-switch chip beside the name: always shows the switch position, so
// "is override on?" never needs reading the status card.
static lv_obj_t* s_sw_chip; static lv_obj_t* s_sw_lbl;
static lv_color_t* s_cbuf;
static int s_zoom = 3600;
static lv_obj_t* s_ax_t[3];
static lv_obj_t* s_ax_h[3];

// big temperature block
static lv_obj_t* s_big_box; static lv_obj_t* s_big_n; static lv_obj_t* s_big_u; static lv_obj_t* s_big_s;
// side stack + pills: a panel with a small key, a value, and an optional icon
struct Cell { lv_obj_t* box; lv_obj_t* k; lv_obj_t* v; lv_obj_t* ic; };
static Cell s_hum, s_coil, s_state, s_comp;

void trend_set_zoom(int seconds) { s_zoom = seconds; screen_trend_refresh(); }
int  trend_zoom(void) { return s_zoom; }

static void panel_style(lv_obj_t* o, int radius) {
    lv_obj_set_style_bg_color(o, th_surface(), 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static void cell_init(Cell& c, lv_obj_t* parent, int x, int y, int w, int h,
                      const char* key, int radius) {
    c.box = lv_obj_create(parent);
    lv_obj_set_size(c.box, w, h);
    lv_obj_set_pos(c.box, x, y);
    panel_style(c.box, radius);
    c.k = lv_label_create(c.box);
    lv_obj_set_pos(c.k, 14, 9);
    lv_obj_set_style_text_color(c.k, th_muted(), 0);
    lv_label_set_text(c.k, key);
    c.v = lv_label_create(c.box);
    lv_obj_set_pos(c.v, 14, 24);
    lv_obj_set_style_text_font(c.v, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(c.v, th_ink(), 0);
    lv_label_set_text(c.v, "--");
    c.ic = nullptr;
}

// The three compact cells (coil, status, compressor) share this tighter
// stack, so the side-column coil card and the row-two cards read as the same
// kind of card.
static void cell_compact(Cell& c) {
    lv_obj_set_pos(c.k, 14, 6);
    lv_obj_set_pos(c.v, 14, 18);
    lv_obj_set_style_text_font(c.v, &lv_font_montserrat_20, 0);
}

// Icons sit right-of-centre. The margin is 8, not the 12 used elsewhere,
// because the longest value ("Running" at montserrat_20) already reaches
// x=103 of the 141px card; 12 would put the icon's left edge exactly on the
// tail of the "g".
static void cell_icon(Cell& c, const lv_image_dsc_t* d) {
    c.ic = lv_image_create(c.box);
    lv_image_set_src(c.ic, d);
    lv_obj_align(c.ic, LV_ALIGN_RIGHT_MID, -8, 0);
    // A8 source: the recolor style IS the fill, so this is not a tint over
    // existing colour -- leave it unset and the icon draws black on black.
    lv_obj_set_style_image_recolor_opa(c.ic, LV_OPA_COVER, 0);
    lv_obj_set_style_image_recolor(c.ic, th_muted(), 0);
}

// Live cells get a tinted fill and a coloured value; at-rest cells stay grey.
// This is the whole visual grammar of the screen in one function. The accent
// trio is a parameter because defrost wears the warning amber, not the
// cooling cyan.
static void cell_set(Cell& c, const char* text, bool live, lv_color_t accent,
                     lv_color_t fill = th_cool_bg(), lv_color_t dim = th_cool_dim()) {
    lv_label_set_text(c.v, text);
    lv_obj_set_style_bg_color(c.box, live ? fill : th_surface(), 0);
    lv_obj_set_style_text_color(c.v, live ? accent : th_ink2(), 0);
    lv_obj_set_style_text_color(c.k, live ? dim : th_muted(), 0);
    if (c.ic) lv_obj_set_style_image_recolor(c.ic, live ? accent : th_muted(), 0);
}

lv_obj_t* screen_trend_create(lv_obj_t* parent) {
    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, 480, 480);
    lv_obj_set_style_bg_color(s_root, th_page(), 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* name = lv_label_create(s_root);
    lv_obj_set_pos(name, PAD, HDR_Y);
    lv_obj_set_style_text_color(name, th_ink2(), 0);
    lv_label_set_text(name, "COOLER32");

    s_sw_chip = lv_obj_create(s_root);
    lv_obj_set_size(s_sw_chip, LV_SIZE_CONTENT, 20);
    lv_obj_set_pos(s_sw_chip, 110, HDR_Y - 3);
    lv_obj_set_style_radius(s_sw_chip, 10, 0);
    lv_obj_set_style_border_width(s_sw_chip, 0, 0);
    lv_obj_set_style_pad_hor(s_sw_chip, 9, 0);
    lv_obj_set_style_pad_ver(s_sw_chip, 0, 0);
    lv_obj_clear_flag(s_sw_chip, LV_OBJ_FLAG_SCROLLABLE);
    s_sw_lbl = lv_label_create(s_sw_chip);
    lv_obj_center(s_sw_lbl);
    lv_label_set_text(s_sw_lbl, "--");

    s_age_lbl = lv_label_create(s_root);
    lv_obj_set_pos(s_age_lbl, 420, HDR_Y);
    lv_obj_set_style_text_color(s_age_lbl, th_muted(), 0);
    lv_label_set_text(s_age_lbl, "--");

    // Temperature: the one figure worth crossing a room for.
    s_big_box = lv_obj_create(s_root);
    lv_obj_set_size(s_big_box, BIG_W, R1_H);
    lv_obj_set_pos(s_big_box, PAD, R1_Y);
    panel_style(s_big_box, 20);
    s_big_n = lv_label_create(s_big_box);
    lv_obj_set_pos(s_big_n, 18, 14);
    lv_obj_set_style_text_font(s_big_n, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_big_n, th_ink(), 0);
    s_big_u = lv_label_create(s_big_box);
    lv_obj_set_pos(s_big_u, 168, 30);
    lv_obj_set_style_text_font(s_big_u, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_big_u, th_muted(), 0);
    lv_label_set_text(s_big_u, "C");
    s_big_s = lv_label_create(s_big_box);
    lv_obj_set_pos(s_big_s, 20, 80);
    lv_obj_set_style_text_color(s_big_s, th_muted(), 0);

    cell_init(s_hum,   s_root, COL3_X, R1_Y,   COL_W, HUM_H, "HUMIDITY",   18);
    cell_init(s_coil,  s_root, COL3_X, COIL_Y, COL_W, R2_H,  "COIL",       16);
    cell_init(s_state, s_root, PAD,    R2_Y,   BIG_W, R2_H,  "--",         16);
    cell_init(s_comp,  s_root, COL3_X, R2_Y,   COL_W, R2_H,  "AC UNIT",    16);

    for (Cell* c : {&s_coil, &s_state, &s_comp}) cell_compact(*c);
    cell_icon(s_coil,  &icon_snowflake);  // the evaporator: ices, defrosts
    cell_icon(s_state, &icon_duty);       // the controller's cycle
    cell_icon(s_comp,  &icon_aircon);     // the unit itself

    s_cbuf = (lv_color_t*)lv_malloc(
        LV_CANVAS_BUF_SIZE(GRAPH_W, GRAPH_H, 16, LV_DRAW_BUF_STRIDE_ALIGN));
    if (s_cbuf) {
        s_canvas = lv_canvas_create(s_root);
        lv_canvas_set_buffer(s_canvas, s_cbuf, GRAPH_W, GRAPH_H, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_pos(s_canvas, GRAPH_X, GRAPH_Y);
    }

    // Axis ticks, coloured to their series so no legend is needed. The left
    // (temperature) scale is neutral, not cyan: cyan means "cooling was
    // requested", and an axis is not a series.
    for (int i = 0; i < 3; i++) {
        const int y = GRAPH_Y - 7 + i * ((GRAPH_H - 1) / 2);
        s_ax_t[i] = lv_label_create(s_root);
        lv_obj_set_pos(s_ax_t[i], 4, y);
        lv_obj_set_style_text_color(s_ax_t[i], th_ink2(), 0);
        lv_label_set_text(s_ax_t[i], "");
        s_ax_h[i] = lv_label_create(s_root);
        lv_obj_set_pos(s_ax_h[i], GRAPH_X + GRAPH_W + 6, y);
        lv_obj_set_style_text_color(s_ax_h[i], th_rh(), 0);
        lv_label_set_text(s_ax_h[i], "");
    }

    static const char* zooms[] = {"1h", "3h", "6h"};
    static const int zsec[] = {3600, 10800, 21600};
    for (int i = 0; i < 3; i++) {
        lv_obj_t* b = lv_button_create(s_root);
        lv_obj_set_size(b, 100, 32);
        lv_obj_set_pos(b, 60 + i * 120, ZOOM_Y);
        lv_obj_set_style_bg_color(b, th_surface(), 0);
        lv_obj_set_style_radius(b, 16, 0);
        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, zooms[i]);
        lv_obj_set_style_text_color(l, th_ink2(), 0);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, [](lv_event_t* e) {
            trend_set_zoom((int)(intptr_t)lv_event_get_user_data(e));
        }, LV_EVENT_CLICKED, (void*)(intptr_t)zsec[i]);
    }

    s_foot_lbl = lv_label_create(s_root);
    lv_obj_set_pos(s_foot_lbl, PAD, FOOT_Y);
    lv_obj_set_style_text_color(s_foot_lbl, th_muted(), 0);

    screen_trend_refresh();
    return s_root;
}

static void axis_clear(void) {
    for (int i = 0; i < 3; i++) { lv_label_set_text(s_ax_t[i], ""); lv_label_set_text(s_ax_h[i], ""); }
}

// "12s" / "4m" / "2h". Terse: it shares the header row.
static void fmt_age(char* out, size_t n, int64_t secs) {
    if (secs < 0)         snprintf(out, n, "--");
    else if (secs < 60)   snprintf(out, n, "%ds", (int)secs);
    else if (secs < 3600) snprintf(out, n, "%dm", (int)(secs / 60));
    else                  snprintf(out, n, "%dh", (int)(secs / 3600));
}

static void draw_graph(void) {
    if (!s_canvas) return;
    lv_layer_t layer;
    lv_canvas_init_layer(s_canvas, &layer);
    lv_canvas_fill_bg(s_canvas, th_surface(), LV_OPA_COVER);

    const CoolerState& st = cooler_state();
    History& h = panel_history();
    if (h.size() < 2) { axis_clear(); lv_canvas_finish_layer(s_canvas, &layer); return; }

    const int64_t to = chart_window_end(h.newest_epoch(), platform_epoch_utc());
    const int64_t from = to - s_zoom;
    static Column cols[NCOLS];
    chart_downsample(h, from, to, cols, NCOLS);

    // Auto-range on the data, always including the setpoint band.
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < NCOLS; i++) {
        if (!cols[i].has) continue;
        if (cols[i].tmin < lo) lo = cols[i].tmin;
        if (cols[i].tmax > hi) hi = cols[i].tmax;
    }
    if (lo > hi) { axis_clear(); lv_canvas_finish_layer(s_canvas, &layer); return; }
    // The band actually in force: override runs the controller's fixed
    // 3..5 C thermostat, whatever coolerset/range say.
    const bool ovr = cooler_in_override(st);
    const float band_lo = ovr ? 3.0f : (float)(st.coolerset - st.range);
    const float band_hi = ovr ? 5.0f : (float)(st.coolerset + st.range);
    if (band_lo < lo) lo = band_lo;
    if (band_hi > hi) hi = band_hi;
    if (hi - lo < 1.0f) { hi = lo + 1.0f; }
    const float pad = (hi - lo) * 0.1f;
    lo -= pad; hi += pad;

    auto y_of = [&](float v) -> int32_t {
        return (int32_t)(GRAPH_H - 1 - ((v - lo) / (hi - lo)) * (GRAPH_H - 1));
    };

    // Humidity gets its OWN vertical range. Temp sits near 14 and RH near 84,
    // so a shared axis would squash the temperature trace into a flat line
    // along the bottom and tell you nothing. Independent scales mean the two
    // traces are each readable; the legend says which is which, and neither
    // trace's absolute height is comparable to the other's -- only its shape.
    float hlo = 1e9f, hhi = -1e9f;
    for (int i = 0; i < NCOLS; i++) {
        if (!cols[i].has) continue;
        if (cols[i].hmin < hlo) hlo = cols[i].hmin;
        if (cols[i].hmax > hhi) hhi = cols[i].hmax;
    }
    const bool have_rh = (hlo <= hhi);
    if (have_rh) {
        if (hhi - hlo < 2.0f) { hhi = hlo + 2.0f; }
        const float hpad = (hhi - hlo) * 0.1f;
        hlo -= hpad; hhi += hpad;
    }
    auto hy_of = [&](float v) -> int32_t {
        return (int32_t)(GRAPH_H - 1 - ((v - hlo) / (hhi - hlo)) * (GRAPH_H - 1));
    };

    // Tick values, top to bottom, now that both ranges are settled.
    char tk[16];
    for (int i = 0; i < 3; i++) {
        snprintf(tk, sizeof(tk), "%.0f", (double)(hi - (hi - lo) * i / 2.0f));
        lv_label_set_text(s_ax_t[i], tk);
        if (have_rh) {
            snprintf(tk, sizeof(tk), "%.0f", (double)(hhi - (hhi - hlo) * i / 2.0f));
            lv_label_set_text(s_ax_h[i], tk);
        } else {
            lv_label_set_text(s_ax_h[i], "");
        }
    }

    // Setpoint band
    lv_draw_rect_dsc_t band;
    lv_draw_rect_dsc_init(&band);
    band.bg_color = lv_color_hex(0x212a26);
    band.bg_opa = LV_OPA_60;
    lv_area_t ba = {0, y_of(band_hi), GRAPH_W - 1, y_of(band_lo)};
    lv_draw_rect(&layer, &band, &ba);

    // Temperature min/max bars, cyan where the controller was requesting
    // cooling for most of the column -- the same cyan the status card wears
    // while cooling, so the card is the legend.
    //
    // Idle stretches are neutral grey rather than a dimmed accent: on a graph
    // the shape of the curve is the data, and it has to stay legible whether
    // or not anything was cooling. th_ink2() sits just under both accents in
    // luminance, so running periods still read as the brighter, saturated
    // ones without idle periods fading out.
    lv_draw_rect_dsc_t bar;
    lv_draw_rect_dsc_init(&bar);
    bar.bg_opa = LV_OPA_COVER;
    const int32_t colw = GRAPH_W / NCOLS;
    for (int i = 0; i < NCOLS; i++) {
        if (!cols[i].has) continue;      // gap stays blank
        bar.bg_color = cols[i].ac ? th_cool() : th_ink2();
        int32_t y0 = y_of(cols[i].tmax);
        int32_t y1 = y_of(cols[i].tmin);
        if (y1 - y0 < 2) y1 = y0 + 2;    // keep flat runs visible
        lv_area_t a = {i * colw, y0, i * colw + colw - 1, y1};
        lv_draw_rect(&layer, &bar, &a);
    }

    // Humidity: a thin trace rather than filled bars, so it reads as an
    // overlay on the temperature rather than competing with it.
    if (have_rh) {
        lv_draw_rect_dsc_t rh;
        lv_draw_rect_dsc_init(&rh);
        rh.bg_color = th_rh();
        rh.bg_opa = LV_OPA_COVER;
        for (int i = 0; i < NCOLS; i++) {
            if (!cols[i].has) continue;    // same honest gap as the temp trace
            const float mid = (cols[i].hmin + cols[i].hmax) * 0.5f;
            int32_t y = hy_of(mid);
            if (y < 0) y = 0;
            if (y > GRAPH_H - 3) y = GRAPH_H - 3;
            lv_area_t a = {i * colw, y, i * colw + colw - 1, y + 2};
            lv_draw_rect(&layer, &rh, &a);
        }
    }
    lv_canvas_finish_layer(s_canvas, &layer);
}

void screen_trend_refresh(void) {
    if (!s_root) return;
    const CoolerState& st = cooler_state();
    const PanelConfig& pc = panel_config();
    char buf[64], age[24];

    const int64_t secs = st.valid ? (platform_epoch_utc() - st.last_rx_epoch) : -1;
    fmt_age(age, sizeof(age), secs);
    lv_label_set_text(s_age_lbl, age);

    const SwitchChip chip = fmt_switch_chip(st);
    lv_label_set_text(s_sw_lbl, chip.text);
    lv_obj_set_style_bg_color(s_sw_chip, chip.alert ? th_warn() : th_surface(), 0);
    lv_obj_set_style_text_color(s_sw_lbl, chip.alert ? th_page() : th_muted(), 0);
    // Stale data is the one non-alarm thing that must still catch the eye.
    lv_obj_set_style_text_color(s_age_lbl,
        (secs < 0 || secs > 90) ? th_warn() : th_muted(), 0);

    if (st.valid && st.temp_valid) snprintf(buf, sizeof(buf), "%.1f", (double)st.temp);
    else                           snprintf(buf, sizeof(buf), "--.-");
    lv_label_set_text(s_big_n, buf);
    // In override the panel's setpoint is NOT in force -- say so where the
    // setpoint normally is, in the warning colour, and name the cause.
    if (st.valid && cooler_in_override(st)) {
        char src[8];
        snprintf(src, sizeof(src), "%s", st.override_src);
        for (char* c = src; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
        snprintf(buf, sizeof(buf), "OVERRIDE 3-5  %s", src);
        lv_obj_set_style_text_color(s_big_s, th_warn(), 0);
    } else {
        snprintf(buf, sizeof(buf), "SET %d  BAND +-%d", st.coolerset, st.range);
        lv_obj_set_style_text_color(s_big_s, th_muted(), 0);
    }
    lv_label_set_text(s_big_s, buf);

    // Humidity: the band from panel config still governs, but as a text
    // colour rather than a filled card -- a red block for "a bit humid" would
    // shout as loudly as a genuine alarm, which is exactly what this
    // direction is trying to stop doing.
    if (st.valid && st.humidity_valid) {
        const int rh = (int)(st.humidity + 0.5f);
        snprintf(buf, sizeof(buf), "%d%%", rh);
        lv_label_set_text(s_hum.v, buf);
        lv_obj_set_style_text_color(s_hum.v,
            rh < pc.hum_low ? th_warn() : rh > pc.hum_high ? th_crit() : th_ink(), 0);
    } else {
        lv_label_set_text(s_hum.v, "--%");
        lv_obj_set_style_text_color(s_hum.v, th_ink2(), 0);
    }

    // Coil: only coloured while it is defrosting -- that is the one moment
    // the fin reading is the most important number on the screen.
    const bool defrost = st.valid && st.defrost;
    if (st.valid && st.fin_temp_valid) snprintf(buf, sizeof(buf), "%.1f", (double)st.fin_temp);
    else                               snprintf(buf, sizeof(buf), "--");
    lv_label_set_text(s_coil.k, defrost ? "COIL  ICE" : "COIL");
    cell_set(s_coil, buf, defrost, th_warn(), th_duty(), th_muted());

    // Status: what the controller is doing and, in the key, which mode.
    char key[48];
    fmt_state_key(st, key, sizeof(key));
    lv_label_set_text(s_state.k, key);
    fmt_state_value(st, buf, sizeof(buf));
    const bool cooling = st.valid && st.relay;
    if (defrost) cell_set(s_state, buf, true, th_warn(), th_duty(), th_muted());
    else         cell_set(s_state, buf, cooling, th_cool());

    // Compressor: what the AC is actually doing, per the fin slope.
    cell_set(s_comp, fmt_compressor(st), st.valid && st.compressor == 1, th_cool());

    char cal[40], d1[16], d2[16];
    fmt_fincal(st, cal, sizeof(cal));
    snprintf(buf, sizeof(buf), "min-off %s   min-run %s   %s",
             fmt_dur((uint32_t)st.minofftime * 60u, d1, sizeof(d1)),
             fmt_dur((uint32_t)st.minruntime, d2, sizeof(d2)), cal);
    lv_label_set_text(s_foot_lbl, buf);

    draw_graph();
}
