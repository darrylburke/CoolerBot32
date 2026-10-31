#include "screen_trend.h"
#include "chart.h"
#include "trend_plot.h"
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

#define ZOOM_Y     390
#define ZOOM_W     50
#define FOOT_Y     430

static lv_obj_t* s_root;
static TrendPlot s_plot;             // the graph, its scales and average readouts
static lv_obj_t* s_foot_lbl;
static lv_obj_t* s_age_lbl;
// Override-switch chip beside the name: always shows the switch position, so
// "is override on?" never needs reading the status card.
static lv_obj_t* s_sw_chip; static lv_obj_t* s_sw_lbl;
static int s_zoom = 10800;
static SeriesShown s_shown;        // both traces; not kept across reboots
static lv_obj_t* s_chip_t;
static lv_obj_t* s_chip_h;
static lv_obj_t* s_chip_a;
static lv_obj_t* s_chip_ha;          // "RH avg": the 1-hour rolling average humidity

static void draw_graph(void);

static void chip_style(lv_obj_t* chip, bool on, lv_color_t ink) {
    lv_obj_set_style_bg_color(chip, th_surface(), 0);
    lv_obj_set_style_bg_opa(chip, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chip, on ? 0 : 1, 0);
    lv_obj_set_style_border_color(chip, th_muted(), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(chip, 0), on ? ink : th_muted(), 0);
}

static void chips_style(void) {
    chip_style(s_chip_t, s_shown.temp, th_ink2());
    chip_style(s_chip_h, s_shown.rh, th_rh());
    chip_style(s_chip_a, s_shown.avg, th_ink());
    chip_style(s_chip_ha, s_shown.rh_avg, th_rh_avg());
}

static lv_obj_t* chip_create(int x, int w, const char* text, Series which) {
    lv_obj_t* b = lv_button_create(s_root);
    lv_obj_set_size(b, w, 32);
    lv_obj_set_pos(b, x, ZOOM_Y);
    lv_obj_set_style_radius(b, 16, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, [](lv_event_t* e) {
        Series w = (Series)(intptr_t)lv_event_get_user_data(e);
        series_toggle(s_shown, w);
        chips_style();
        draw_graph();
    }, LV_EVENT_CLICKED, (void*)(intptr_t)which);
    return b;
}

// big temperature block
static lv_obj_t* s_big_box; static lv_obj_t* s_big_n; static lv_obj_t* s_big_u; static lv_obj_t* s_big_s;
static lv_obj_t* s_big_a;   // "/ 4.0": the 1-hour rolling average after the reading
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
    s_big_a = lv_label_create(s_big_box);
    lv_obj_set_style_text_font(s_big_a, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_big_a, th_ink(), 0);   // the average line's colour
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

    trend_plot_create(s_plot, s_root, GRAPH_X, GRAPH_Y, GRAPH_W, GRAPH_H);

    static const char* zooms[] = {"1h", "3h", "6h", "12h"};
    static const int zsec[] = {3600, 10800, 21600, 43200};
    for (int i = 0; i < 4; i++) {
        lv_obj_t* b = lv_button_create(s_root);
        lv_obj_set_size(b, ZOOM_W, 32);
        lv_obj_set_pos(b, PAD + i * (ZOOM_W + 4), ZOOM_Y);
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

    // Show/hide chips, coloured like their traces; dim and outlined when off.
    // Four chips and four zooms share the row: 4 * 50 + 3 * 4 for the zooms,
    // then the chips sized to their words, ending on the right margin (464).
    s_chip_t  = chip_create(236, 52, "Temp",   Series::Temp);
    s_chip_h  = chip_create(294, 44, "RH",     Series::Rh);
    s_chip_a  = chip_create(344, 48, "Avg",    Series::Avg);
    s_chip_ha = chip_create(398, 66, "RH avg", Series::RhAvg);
    chips_style();

    s_foot_lbl = lv_label_create(s_root);
    lv_obj_set_pos(s_foot_lbl, PAD, FOOT_Y);
    lv_obj_set_style_text_color(s_foot_lbl, th_muted(), 0);

    screen_trend_refresh();
    return s_root;
}

// "12s" / "4m" / "2h". Terse: it shares the header row.
static void fmt_age(char* out, size_t n, int64_t secs) {
    if (secs < 0)         snprintf(out, n, "--");
    else if (secs < 60)   snprintf(out, n, "%ds", (int)secs);
    else if (secs < 3600) snprintf(out, n, "%dm", (int)(secs / 60));
    else                  snprintf(out, n, "%dh", (int)(secs / 3600));
}

static void draw_graph(void) {
    const CoolerState& st = cooler_state();
    TrendPlotOpts o;
    o.shown = s_shown;
    o.zoom_s = s_zoom;
    // The band actually in force: override runs the controller's fixed
    // 3..5 C thermostat, whatever coolerset/range say.
    const bool ovr = cooler_in_override(st);
    o.band = true;
    o.band_lo = ovr ? 3.0f : (float)(st.coolerset - st.range);
    o.band_hi = ovr ? 5.0f : (float)(st.coolerset + st.range);
    trend_plot_draw(s_plot, panel_history(), platform_epoch_utc(), o);
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
    // Current / 1-hour rolling average: the right-hand end of the graph's
    // average line, or "-.-" until a whole hour is on record. The unit follows
    // whatever width the two numbers take.
    {
        float avg = 0;
        char abuf[16], line[24];
        // Two statements on purpose: as one call's arguments, `avg` could be
        // read before chart_latest_avg() fills it (evaluation order is unset).
        const bool has_avg = chart_latest_avg(panel_history(), platform_epoch_utc(), avg);
        chart_avg_text(has_avg, avg, abuf, sizeof abuf);
        snprintf(line, sizeof line, "/ %s", abuf);
        lv_label_set_text(s_big_a, line);
        lv_obj_update_layout(s_big_box);
        lv_obj_align_to(s_big_a, s_big_n, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, -6);
        lv_obj_align_to(s_big_u, s_big_a, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, 0);
    }
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
