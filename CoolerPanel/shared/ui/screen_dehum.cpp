#include "screen_dehum.h"
#include "trend_plot.h"
#include "chart.h"
#include "history.h"
#include "dehum_state.h"
#include "dehum_status.h"
#include "dehum_commands.h"
#include "app.h"
#include "theme.h"
#include "platform.h"
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// The dehumidifier, at parity with the app's Temp/Humidity tab (spec
// 2026-10-31-dehum-rules-and-temp-humidity-tab-design.md §3). Its own page:
// the dehumidifier being away is shown here, never as a Cooler alarm.
//
//   8..30    header: page name, status strip (online / offline / waiting,
//            "Broker link lost: off")
//   36..72   Graph | Controls
//   80..480  the sub-page
//
// Graph (fixed, Quiet Slate like the Trend page): temperature / 1-h average,
// humidity / 1-h average, the status words, and the trend with both averages,
// enabled rules' thresholds dashed and relay 1's running time shaded.
//
// Controls (scrolls: four rule cards do not fit 400 px at finger size): mode
// buttons sent at once; Timer / Cycle run / Cycle rest / Min run steppers and
// the rule fields go through DehumCommands (debounce, pending until
// dehum/data echoes them, "Not applied" after the deadline). Everything is
// disabled unless the dehumidifier is online.
// ---------------------------------------------------------------------------
#define PAD        16
#define HDR_Y      8
#define TAB_Y      36
#define TAB_H      36
#define PAGE_Y     80
#define PAGE_H     (480 - PAGE_Y)

// Graph sub-page, in page coordinates
#define RD_Y       2
#define RD_H       76
#define BIG_W      296
#define RH_X       (PAD + BIG_W + 12)
#define RH_W       (464 - RH_X)
#define LINE_Y     86
#define GRAPH_X    36
#define GRAPH_Y    120
#define GRAPH_W    408
#define GRAPH_H    192
#define ZOOM_Y     (GRAPH_Y + GRAPH_H + 10)
#define ZOOM_W     50
#define NOTE_Y     (ZOOM_Y + 42)

// Controls sub-page, in its scrolled content's coordinates
#define ROW_H      56
#define BTN_H      44
#define CARD_W     448
#define CARD_H     206
#define CARD_GAP   12
#define SEG_W      100
#define TOAST_MS   4000

static lv_obj_t* s_root;
static lv_obj_t* s_strip;  static lv_obj_t* s_strip_lbl;
static lv_obj_t* s_toast;
static lv_obj_t* s_tab_btn[2];
static lv_obj_t* s_page[2];
static int s_tab = 0;

// ---- Graph ----
static lv_obj_t* s_big_box; static lv_obj_t* s_big_n; static lv_obj_t* s_big_a; static lv_obj_t* s_big_u;
static lv_obj_t* s_rh_box;  static lv_obj_t* s_rh_n;  static lv_obj_t* s_rh_a;
static lv_obj_t* s_line;
static TrendPlot s_plot;
static SeriesShown s_shown;
static int s_zoom = 10800;
static lv_obj_t* s_chips[4];
static const Series kChipSeries[4] = {Series::Temp, Series::Rh, Series::Avg, Series::RhAvg};

// ---- Controls ----
static lv_obj_t* s_offline_note;
static lv_obj_t* s_mode_btn[DEHUM_MODE_BUTTONS_N];
// One - / + row per stepped key (the four settings and the four thresholds).
struct StepRow { int idx; lv_obj_t* val; lv_obj_t* minus; lv_obj_t* plus; };
static StepRow s_steps[DEHUM_PLAIN_N + DEHUM_RULES];
static int s_nsteps = 0;
struct StepCtx { int idx; int dir; };
static StepCtx s_step_ctx[(DEHUM_PLAIN_N + DEHUM_RULES) * 2];
// Two-way choices on the rule cards: sensor, above/below, relay, on/off.
struct Choice { int idx; int value; const char* text; lv_obj_t* btn; lv_obj_t* lbl; };
static Choice s_choices[DEHUM_RULES * 4 * 2];
static int s_nchoices = 0;
struct RuleCard { lv_obj_t* card; lv_obj_t* met; lv_obj_t* sw; lv_obj_t* sw_pend; };
static RuleCard s_rules[DEHUM_RULES];

static char s_toast_msg[96];
static bool s_toast_active = false;
static uint32_t s_toast_started_ms = 0;

// ---------------------------------------------------------------- helpers
static void box_style(lv_obj_t* o, int radius, lv_color_t bg) {
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t* label(lv_obj_t* parent, int x, int y, const char* text, lv_color_t ink,
                       const lv_font_t* font = nullptr) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_text_color(l, ink, 0);
    if (font) lv_obj_set_style_text_font(l, font, 0);
    lv_label_set_text(l, text);
    return l;
}

// A flat button: no theme blue or shadow, and half-faded while disabled (the
// local bg colour would otherwise hide the theme's disabled look).
static lv_obj_t* button(lv_obj_t* parent, int x, int y, int w, int h, const char* text,
                        lv_obj_t** lbl_out = nullptr) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_radius(b, h / 2, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, th_duty(), 0);
    lv_obj_set_style_opa(b, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, th_ink(), 0);
    lv_obj_center(l);
    if (lbl_out) *lbl_out = l;
    return b;
}

static void set_enabled(lv_obj_t* o, bool on) {
    if (on) lv_obj_remove_state(o, LV_STATE_DISABLED);
    else    lv_obj_add_state(o, LV_STATE_DISABLED);
}

// Selected: the live accent; otherwise grey. The Settings page's grammar.
static void select_style(lv_obj_t* b, lv_obj_t* l, bool sel) {
    lv_obj_set_style_bg_color(b, sel ? th_cool_bg() : th_duty(), 0);
    lv_obj_set_style_border_width(b, sel ? 1 : 0, 0);
    lv_obj_set_style_border_color(b, th_cool(), 0);
    lv_obj_set_style_text_color(l, sel ? th_cool() : th_ink2(), 0);
}

static lv_obj_t* heading(lv_obj_t* parent, int y, const char* text) {
    return label(parent, PAD, y, text, th_cool(), &lv_font_montserrat_20);
}

// ---------------------------------------------------------------- events
static void on_tab(lv_event_t* e) { screen_dehum_show_tab((int)(intptr_t)lv_event_get_user_data(e)); }

static void on_mode(lv_event_t* e) {
    const DehumMode m = DEHUM_MODE_BUTTONS[(int)(intptr_t)lv_event_get_user_data(e)];
    dehum_commands().set_mode(m, dehum_controls_enabled());
    screen_dehum_refresh();
}

static void on_step(lv_event_t* e) {
    const StepCtx* c = (const StepCtx*)lv_event_get_user_data(e);
    dehum_commands().step(c->idx, c->dir, dehum_state(), dehum_controls_enabled(), platform_now_ms());
    screen_dehum_refresh();
}

static void on_choice(lv_event_t* e) {
    const Choice* c = (const Choice*)lv_event_get_user_data(e);
    DehumCommands& cmd = dehum_commands();
    int cur;
    if (cmd.display_value(c->idx, dehum_state(), cur) && cur == c->value) return;
    cmd.enter(c->idx, c->value, dehum_state(), dehum_controls_enabled(), platform_now_ms());
    screen_dehum_refresh();
}

static void on_switch(lv_event_t* e) {
    const int n = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
    const bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    dehum_commands().enter(dehum_rule_key(n, RF_EN), on ? 1 : 0, dehum_state(),
                           dehum_controls_enabled(), platform_now_ms());
    screen_dehum_refresh();   // also puts the switch back if that was refused
}

static void draw_graph(void);

static void chips_style(void) {
    const bool on[4] = {s_shown.temp, s_shown.rh, s_shown.avg, s_shown.rh_avg};
    const lv_color_t ink[4] = {th_ink2(), th_rh(), th_ink(), th_rh_avg()};
    for (int i = 0; i < 4; i++) {
        lv_obj_t* c = s_chips[i];
        lv_obj_set_style_bg_color(c, th_surface(), 0);
        lv_obj_set_style_bg_opa(c, on[i] ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(c, on[i] ? 0 : 1, 0);
        lv_obj_set_style_border_color(c, th_muted(), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(c, 0), on[i] ? ink[i] : th_muted(), 0);
    }
}

static void on_chip(lv_event_t* e) {
    series_toggle(s_shown, kChipSeries[(int)(intptr_t)lv_event_get_user_data(e)]);
    chips_style();
    draw_graph();
}

static void on_zoom(lv_event_t* e) {
    s_zoom = (int)(intptr_t)lv_event_get_user_data(e);
    draw_graph();
}

// ---------------------------------------------------------------- build
static lv_obj_t* make_page(void) {
    lv_obj_t* p = lv_obj_create(s_root);
    lv_obj_set_size(p, 480, PAGE_H);
    lv_obj_set_pos(p, 0, PAGE_Y);
    lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_radius(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

static void build_graph(lv_obj_t* pg) {
    // Temperature / its 1-hour average, as the Trend page's big block.
    s_big_box = lv_obj_create(pg);
    lv_obj_set_size(s_big_box, BIG_W, RD_H);
    lv_obj_set_pos(s_big_box, PAD, RD_Y);
    box_style(s_big_box, 20, th_surface());
    s_big_n = label(s_big_box, 18, 8, "--.-", th_ink(), &lv_font_montserrat_48);
    s_big_a = label(s_big_box, 0, 0, "", th_ink(), &lv_font_montserrat_28);
    s_big_u = label(s_big_box, 0, 0, "°C", th_muted(), &lv_font_montserrat_28);

    // Humidity / its average, in their trace colours.
    s_rh_box = lv_obj_create(pg);
    lv_obj_set_size(s_rh_box, RH_W, RD_H);
    lv_obj_set_pos(s_rh_box, RH_X, RD_Y);
    box_style(s_rh_box, 18, th_surface());
    label(s_rh_box, 14, 8, "HUMIDITY %RH", th_muted());
    s_rh_n = label(s_rh_box, 14, 30, "--", th_ink(), &lv_font_montserrat_28);
    s_rh_a = label(s_rh_box, 0, 0, "", th_rh_avg(), &lv_font_montserrat_20);

    s_line = label(pg, PAD, LINE_Y, "--", th_ink2());
    lv_obj_set_width(s_line, 448);
    lv_label_set_long_mode(s_line, LV_LABEL_LONG_WRAP);

    trend_plot_create(s_plot, pg, GRAPH_X, GRAPH_Y, GRAPH_W, GRAPH_H);

    static const char* zooms[] = {"1h", "3h", "6h", "12h"};
    static const int zsec[] = {3600, 10800, 21600, 43200};
    for (int i = 0; i < 4; i++) {
        lv_obj_t* l;
        lv_obj_t* b = button(pg, PAD + i * (ZOOM_W + 4), ZOOM_Y, ZOOM_W, 32, zooms[i], &l);
        lv_obj_set_style_bg_color(b, th_surface(), 0);
        lv_obj_set_style_text_color(l, th_ink2(), 0);
        lv_obj_add_event_cb(b, on_zoom, LV_EVENT_CLICKED, (void*)(intptr_t)zsec[i]);
    }
    static const char* names[4] = {"Temp", "RH", "Avg", "RH avg"};
    static const int cx[4] = {236, 294, 344, 398}, cw[4] = {52, 44, 48, 66};
    for (int i = 0; i < 4; i++) {
        s_chips[i] = button(pg, cx[i], ZOOM_Y, cw[i], 32, names[i]);
        lv_obj_set_style_radius(s_chips[i], 16, 0);
        lv_obj_add_event_cb(s_chips[i], on_chip, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
    chips_style();

    lv_obj_t* note = label(pg, PAD, NOTE_Y,
        "Dashed: enabled rules' thresholds. Shaded: relay 1 running.", th_muted());
    lv_obj_set_width(note, 448);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
}

static void add_stepper(lv_obj_t* parent, int idx, int x_lbl, int x_val, int x_btn, int y,
                        const char* text) {
    label(parent, x_lbl, y + 14, text, th_ink());
    StepRow& r = s_steps[s_nsteps];
    r.idx = idx;
    r.val = label(parent, x_val, y + 14, "--", th_ink2());
    for (int d = 0; d < 2; d++) {
        lv_obj_t* l;
        lv_obj_t* b = button(parent, x_btn + d * 70, y, 60, BTN_H, d == 0 ? "-" : "+", &l);
        lv_obj_set_style_radius(b, 12, 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
        StepCtx& c = s_step_ctx[s_nsteps * 2 + d];
        c = StepCtx{idx, d == 0 ? -1 : +1};
        // Short tap = one step; hold to run through a 0..100 range.
        lv_obj_add_event_cb(b, on_step, LV_EVENT_SHORT_CLICKED, &c);
        lv_obj_add_event_cb(b, on_step, LV_EVENT_LONG_PRESSED_REPEAT, &c);
        (d == 0 ? r.minus : r.plus) = b;
    }
    s_nsteps++;
}

static void add_choice(lv_obj_t* parent, int idx, int value, const char* text, int x, int y) {
    Choice& c = s_choices[s_nchoices++];
    c.idx = idx; c.value = value; c.text = text;
    c.btn = button(parent, x, y, SEG_W, 40, text, &c.lbl);
    lv_obj_add_event_cb(c.btn, on_choice, LV_EVENT_CLICKED, &c);
}

static void build_rule(lv_obj_t* page, int n, int y) {
    RuleCard& rc = s_rules[n - 1];
    rc.card = lv_obj_create(page);
    lv_obj_set_size(rc.card, CARD_W, CARD_H);
    lv_obj_set_pos(rc.card, PAD, y);
    box_style(rc.card, 16, th_surface());   // not scrollable: a drag on it scrolls the page
    lv_obj_t* c = rc.card;

    char t[12];
    std::snprintf(t, sizeof t, "Rule %d", n);
    label(c, 14, 12, t, th_ink(), &lv_font_montserrat_20);
    rc.met = lv_label_create(c);
    lv_obj_set_pos(rc.met, 96, 13);
    lv_obj_set_style_bg_color(rc.met, th_good(), 0);
    lv_obj_set_style_bg_opa(rc.met, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(rc.met, th_page(), 0);
    lv_obj_set_style_radius(rc.met, 9, 0);
    lv_obj_set_style_pad_hor(rc.met, 8, 0);
    lv_obj_set_style_pad_ver(rc.met, 1, 0);
    lv_label_set_text(rc.met, "met");

    rc.sw_pend = label(c, 282, 15, "", th_muted());
    label(c, 302, 15, "Enabled", th_ink2());
    rc.sw = lv_switch_create(c);
    lv_obj_set_size(rc.sw, 60, 32);
    lv_obj_set_pos(rc.sw, CARD_W - 14 - 60, 8);
    lv_obj_set_style_bg_color(rc.sw, th_duty(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(rc.sw, th_cool(), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_opa(rc.sw, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_add_event_cb(rc.sw, on_switch, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)n);

    const int x2 = 14 + 2 * SEG_W + 16;   // second pair of choices
    add_choice(c, dehum_rule_key(n, RF_SENSOR), DEHUM_SENSOR_TEMP, "Temp",     14,               52);
    add_choice(c, dehum_rule_key(n, RF_SENSOR), DEHUM_SENSOR_HUM,  "Humidity", 14 + SEG_W + 4,   52);
    add_choice(c, dehum_rule_key(n, RF_CMP),    1,                 "Above",    x2,               52);
    add_choice(c, dehum_rule_key(n, RF_CMP),    0,                 "Below",    x2 + SEG_W + 4,   52);
    add_stepper(c, dehum_rule_key(n, RF_VALUE), 14, 120, 290, 100, "Threshold");
    add_choice(c, dehum_rule_key(n, RF_RELAY),  1,                 "Relay 1",  14,               154);
    add_choice(c, dehum_rule_key(n, RF_RELAY),  2,                 "Relay 2",  14 + SEG_W + 4,   154);
    add_choice(c, dehum_rule_key(n, RF_ACTION), 1,                 "Turn on",  x2,               154);
    add_choice(c, dehum_rule_key(n, RF_ACTION), 0,                 "Turn off", x2 + SEG_W + 4,   154);
}

static void build_controls(lv_obj_t* pg) {
    lv_obj_add_flag(pg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(pg, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(pg, LV_SCROLLBAR_MODE_ACTIVE);

    s_offline_note = label(pg, PAD, 6, "Controls work only while the dehumidifier is online.", th_warn());

    int y = 32;
    heading(pg, y, "Mode");
    y += 30;
    static const int mw[DEHUM_MODE_BUTTONS_N] = {64, 64, 84, 132, 84};
    int x = PAD;
    for (int i = 0; i < DEHUM_MODE_BUTTONS_N; i++) {
        s_mode_btn[i] = button(pg, x, y, mw[i], BTN_H, dehum_mode_label(DEHUM_MODE_BUTTONS[i]));
        lv_obj_add_event_cb(s_mode_btn[i], on_mode, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        x += mw[i] + 5;
    }
    y += BTN_H + 18;

    heading(pg, y, "Settings");
    y += 30;
    for (int i = 0; i < DEHUM_PLAIN_N; i++) {
        add_stepper(pg, i, PAD, 200, 330, y, DEHUM_PLAIN[i].label);
        y += ROW_H;
    }
    y += 8;

    heading(pg, y, "Rules");
    y += 30;
    lv_obj_t* note = label(pg, PAD, y,
        "Rules apply only in AUTO. Off wins when rules disagree; with no rule met the relay is off.",
        th_muted());
    lv_obj_set_width(note, 448);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    y += 46;
    for (int n = 1; n <= DEHUM_RULES; n++) {
        build_rule(pg, n, y);
        y += CARD_H + CARD_GAP;
    }
    // Bottom margin: the last card can scroll clear of the edge.
    lv_obj_t* spacer = lv_obj_create(pg);
    lv_obj_set_size(spacer, 1, 4);
    lv_obj_set_pos(spacer, 0, y);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
}

lv_obj_t* screen_dehum_create(lv_obj_t* parent) {
    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, 480, 480);
    box_style(s_root, 0, th_page());

    label(s_root, PAD, HDR_Y, "TEMP/HUMIDITY", th_ink2());
    s_strip = lv_obj_create(s_root);
    lv_obj_set_size(s_strip, LV_SIZE_CONTENT, 22);
    box_style(s_strip, 11, th_surface());
    lv_obj_set_style_pad_hor(s_strip, 10, 0);
    s_strip_lbl = lv_label_create(s_strip);
    lv_obj_center(s_strip_lbl);
    lv_label_set_text(s_strip_lbl, "--");

    static const char* tabs[2] = {"Graph", "Controls"};
    for (int t = 0; t < 2; t++) {
        lv_obj_t* l;
        s_tab_btn[t] = button(s_root, PAD + t * 228, TAB_Y, 220, TAB_H, tabs[t], &l);
        lv_obj_add_event_cb(s_tab_btn[t], on_tab, LV_EVENT_CLICKED, (void*)(intptr_t)t);
        s_page[t] = make_page();
    }
    build_graph(s_page[0]);
    build_controls(s_page[1]);

    // Above the header, like the Settings page's toast.
    s_toast = lv_label_create(s_root);
    lv_obj_set_pos(s_toast, PAD, 4);
    lv_obj_set_width(s_toast, 448);
    lv_obj_set_style_bg_color(s_toast, th_crit(), 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_toast, th_ink(), 0);
    lv_obj_set_style_pad_all(s_toast, 4, 0);
    lv_obj_set_style_radius(s_toast, 6, 0);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);

    screen_dehum_show_tab(0);
    return s_root;
}

void screen_dehum_show_tab(int tab) {
    if (!s_root || tab < 0 || tab > 1) return;
    s_tab = tab;
    for (int i = 0; i < 2; i++) {
        if (i == tab) lv_obj_remove_flag(s_page[i], LV_OBJ_FLAG_HIDDEN);
        else          lv_obj_add_flag(s_page[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_tab_btn[i], i == tab ? th_cool_bg() : th_surface(), 0);
    }
    screen_dehum_refresh();
}

void screen_dehum_scroll_to(int y) {
    if (!s_root) return;
    lv_obj_update_layout(s_page[1]);
    lv_obj_scroll_to_y(s_page[1], y, LV_ANIM_OFF);
}

// ---------------------------------------------------------------- refresh
static void draw_graph(void) {
    const DehumState& st = dehum_state();
    // A dashed line at each enabled rule's threshold, on its sensor's scale
    // (from dehum/data, as the app draws them).
    float tl[DEHUM_RULES], hl[DEHUM_RULES];
    size_t nt = 0, nh = 0;
    const DehumValues& v = st.settings;
    for (int n = 1; n <= DEHUM_RULES && st.valid; n++) {
        const int en = dehum_rule_key(n, RF_EN), se = dehum_rule_key(n, RF_SENSOR),
                  va = dehum_rule_key(n, RF_VALUE);
        if (!v.known[en] || v.v[en] != 1 || !v.known[se] || !v.known[va]) continue;
        if (v.v[se] == DEHUM_SENSOR_TEMP) tl[nt++] = (float)v.v[va];
        else if (v.v[se] == DEHUM_SENSOR_HUM) hl[nh++] = (float)v.v[va];
    }
    TrendPlotOpts o;
    o.shown = s_shown;
    o.zoom_s = s_zoom;
    o.shade_relay = true;
    o.t_lines = tl; o.n_t = nt;
    o.h_lines = hl; o.n_h = nh;
    trend_plot_draw(s_plot, dehum_history(), platform_epoch_utc(), o);
}

static void refresh_strip(DehumView view, bool link_lost) {
    lv_color_t bg = th_surface(), fg = th_muted();
    char buf[72];
    std::snprintf(buf, sizeof buf, "%s", dehum_view_text(view));
    if (view == DehumView::Online) fg = th_good();
    else if (view != DehumView::Waiting) { bg = th_crit(); fg = th_ink(); }
    if (link_lost && view != DehumView::BrokerDown) {
        // The dehumidifier says it lost the broker, so both relays are off.
        std::snprintf(buf, sizeof buf, "%s  •  Broker link lost: off", dehum_view_text(view));
        bg = th_warn(); fg = th_page();
    }
    lv_label_set_text(s_strip_lbl, buf);
    lv_obj_set_style_bg_color(s_strip, bg, 0);
    lv_obj_set_style_text_color(s_strip_lbl, fg, 0);
    lv_obj_update_layout(s_strip);
    lv_obj_align(s_strip, LV_ALIGN_TOP_RIGHT, -PAD, HDR_Y - 3);
}

static void refresh_graph(const DehumState& st) {
    const int64_t now = platform_epoch_utc();
    const History& h = dehum_history();
    char buf[64], ab[16];
    float avg = 0;

    if (st.valid && st.temp_valid) std::snprintf(buf, sizeof buf, "%.1f", (double)st.temp);
    else                           std::snprintf(buf, sizeof buf, "--.-");
    lv_label_set_text(s_big_n, buf);
    // Two statements on purpose: avg must be filled before it is read.
    bool has = chart_latest_avg(h, now, avg, AvgOf::Temp);
    chart_avg_text(has, avg, ab, sizeof ab);
    std::snprintf(buf, sizeof buf, "/ %s", ab);
    lv_label_set_text(s_big_a, buf);
    lv_obj_update_layout(s_big_box);
    lv_obj_align_to(s_big_a, s_big_n, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, -6);
    lv_obj_align_to(s_big_u, s_big_a, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, 0);

    if (st.valid && st.humidity_valid) std::snprintf(buf, sizeof buf, "%d", (int)(st.humidity + 0.5f));
    else                               std::snprintf(buf, sizeof buf, "--");
    lv_label_set_text(s_rh_n, buf);
    has = chart_latest_avg(h, now, avg, AvgOf::Rh);
    chart_avg_text(has, avg, ab, sizeof ab);
    std::snprintf(buf, sizeof buf, "/ %s", ab);
    lv_label_set_text(s_rh_a, buf);
    lv_obj_update_layout(s_rh_box);
    lv_obj_align_to(s_rh_a, s_rh_n, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -3);

    char line[128];
    dehum_status_line(st, line, sizeof line);
    lv_label_set_text(s_line, line);

    draw_graph();
}

static void refresh_controls(const DehumState& st, bool enabled) {
    DehumCommands& cmd = dehum_commands();
    const DehumValues cur = cmd.display_values(st);
    char buf[48];

    if (enabled) lv_obj_add_flag(s_offline_note, LV_OBJ_FLAG_HIDDEN);
    else         lv_obj_remove_flag(s_offline_note, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < DEHUM_MODE_BUTTONS_N; i++) {
        select_style(s_mode_btn[i], lv_obj_get_child(s_mode_btn[i], 0),
                     st.valid && st.mode == DEHUM_MODE_BUTTONS[i]);
        set_enabled(s_mode_btn[i], enabled);
    }

    for (int i = 0; i < s_nsteps; i++) {
        const StepRow& r = s_steps[i];
        const bool pending = cmd.is_pending(r.idx);
        dehum_format(r.idx, cur.v[r.idx], cur.known[r.idx], cur, buf, sizeof buf - 4);
        if (pending) std::strcat(buf, " ...");
        lv_label_set_text(r.val, buf);
        lv_obj_set_style_text_opa(r.val, (pending || !enabled) ? LV_OPA_50 : LV_OPA_COVER, 0);
        const bool can = enabled && cur.known[r.idx];
        set_enabled(r.minus, can);
        set_enabled(r.plus, can);
    }

    for (int i = 0; i < s_nchoices; i++) {
        const Choice& c = s_choices[i];
        const bool sel = cur.known[c.idx] && cur.v[c.idx] == c.value;
        select_style(c.btn, c.lbl, sel);
        // A pending choice says so on the chip it is waiting for.
        if (sel && cmd.is_pending(c.idx)) {
            std::snprintf(buf, sizeof buf, "%s ...", c.text);
            lv_label_set_text(c.lbl, buf);
        } else {
            lv_label_set_text(c.lbl, c.text);
        }
        set_enabled(c.btn, enabled);
    }

    for (int n = 1; n <= DEHUM_RULES; n++) {
        RuleCard& rc = s_rules[n - 1];
        const int en = dehum_rule_key(n, RF_EN);
        const bool on = cur.known[en] && cur.v[en] == 1;
        if (on) lv_obj_add_state(rc.sw, LV_STATE_CHECKED);
        else    lv_obj_remove_state(rc.sw, LV_STATE_CHECKED);
        set_enabled(rc.sw, enabled && cur.known[en]);
        lv_label_set_text(rc.sw_pend, cmd.is_pending(en) ? "..." : "");
        // "met" while its condition holds -- only meaningful for an enabled rule.
        if (on && st.valid && st.rule_match[n - 1]) lv_obj_remove_flag(rc.met, LV_OBJ_FLAG_HIDDEN);
        else                                        lv_obj_add_flag(rc.met, LV_OBJ_FLAG_HIDDEN);
        // A disabled rule's card recedes.
        lv_obj_set_style_bg_color(rc.card, on ? th_surface() : th_page(), 0);
        lv_obj_set_style_border_width(rc.card, on ? 0 : 1, 0);
        lv_obj_set_style_border_color(rc.card, th_grid(), 0);
    }
}

void screen_dehum_refresh(void) {
    if (!s_root) return;
    const DehumState& st = dehum_state();
    const DehumView view = dehum_health(app_link_state(), st, platform_epoch_utc());
    refresh_strip(view, st.valid && st.link_lost);
    refresh_graph(st);
    refresh_controls(st, view == DehumView::Online);

    char t[96];
    if (dehum_commands().take_toast(t, sizeof t)) {
        std::snprintf(s_toast_msg, sizeof s_toast_msg, "%s", t);
        s_toast_active = true;
        s_toast_started_ms = platform_now_ms();
    }
    if (s_toast_active && platform_now_ms() - s_toast_started_ms < TOAST_MS) {
        lv_label_set_text(s_toast, s_toast_msg);
        lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_toast);
    } else {
        s_toast_active = false;
        lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    }
}
