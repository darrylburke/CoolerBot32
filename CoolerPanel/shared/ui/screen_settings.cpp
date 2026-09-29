#include "screen_settings.h"
#include "bounds.h"
#include "commands.h"
#include "cooler_state.h"
#include "app.h"
#include "platform.h"
#include "theme.h"
#include <cassert>
#include <cstdio>
#include <cstring>

// Ten settable values do not fit one fixed 480 px page at a finger-sized row
// height, and every screen here deliberately refuses to scroll. So the page
// is split into three tabs by what the values are ABOUT (bounds.h Group):
//
//   Box    -- the target: set point, band, history spacing
//   Coil   -- icing protection: cutoff, clear, settle
//   Timing -- compressor protection and the sensor-failure fallback
//
// Unlike v3, the v4 controller always publishes its stored settings (never
// mode-dependent effective values), so nothing here is read-only in override.
// Override's fixed 3-5 C thermostat simply ignores coolerset/range, and the
// banner says so.
#define BANNER_Y   6
#define TAB_Y      36
#define TAB_H      40
#define ROW0_Y     88
#define ROW_H      56
#define CHIP_ROW_H 40
#define TOAST_MS   4000   // how long a take_toast() message stays on screen
#define MAX_ROWS   16

static const int  kSampleVals[]        = {10, 60, 300, 900, 3600};
static const char* const kSampleLbls[] = {"10s", "1m", "5m", "15m", "1h"};
static const int  kSampleN = 5;

static const char* const kTabNames[] = {"Box", "Coil", "Timing"};
static const char* const kTabHints[] = {
    "Set point and band apply in normal mode.\nOverride holds a fixed 3-5 C.",
    "Cooling stops when the coil reaches Ice cutoff\nand resumes once it is above Ice clear.",
    "Set Min off / Min run from the AC timing test.\nMax run and Backup duty only apply if a sensor fails.",
};

static lv_obj_t* s_root;
static lv_obj_t* s_banner;
static lv_obj_t* s_toast;
static lv_obj_t* s_tab_btn[3];
static lv_obj_t* s_page[3];
static int s_tab = 0;

// One stepper row per Widget::Stepper bound, indexed like BOUNDS[].
static lv_obj_t* s_val[MAX_ROWS];
static lv_obj_t* s_minus[MAX_ROWS];
static lv_obj_t* s_plus[MAX_ROWS];

struct StepCtx { const Bound* b; int dir; };
static StepCtx s_ctx[MAX_ROWS * 2];

struct ChipCtx { const char* key; int value; };
static ChipCtx s_chips[kSampleN];
static lv_obj_t* s_chip_obj[kSampleN];

// take_toast() is one-shot (Commands clears it once read), so its message
// must be latched here together with an expiry, not re-read every refresh.
static char s_toast_msg[64] = {0};
static bool s_toast_active = false;
static uint32_t s_toast_started_ms = 0;

static void on_step(lv_event_t* e) {
    StepCtx* c = (StepCtx*)lv_event_get_user_data(e);
    const CoolerState& st = cooler_state();
    Commands& cmd = panel_commands();
    int cur = cmd.display_value(c->b->key, st);
    int step = c->b->step ? c->b->step : 1;
    cmd.request(c->b->key, cur + c->dir * step, platform_now_ms());
    screen_settings_refresh();
}

static void on_chip(lv_event_t* e) {
    ChipCtx* c = (ChipCtx*)lv_event_get_user_data(e);
    panel_commands().request(c->key, c->value, platform_now_ms());
    screen_settings_refresh();
}

static void show_tab(int t) {
    s_tab = t;
    for (int i = 0; i < 3; i++) {
        if (i == t) lv_obj_remove_flag(s_page[i], LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_add_flag(s_page[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_tab_btn[i], i == t ? th_cool_bg() : th_surface(), 0);
    }
}

static void on_tab(lv_event_t* e) {
    show_tab((int)(intptr_t)lv_event_get_user_data(e));
    screen_settings_refresh();
}

static lv_obj_t* make_page(lv_obj_t* parent) {
    lv_obj_t* p = lv_obj_create(parent);
    lv_obj_set_size(p, 472, 480 - ROW0_Y - 4);
    lv_obj_set_pos(p, 0, ROW0_Y);
    lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

lv_obj_t* screen_settings_create(lv_obj_t* parent) {
    assert(BOUNDS_N <= MAX_ROWS && "screen_settings: BOUNDS[] outgrew the fixed-size UI arrays");

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, 480, 480);
    lv_obj_set_style_bg_color(s_root, th_page(), 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 4, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    for (int t = 0; t < 3; t++) {
        lv_obj_t* b = lv_button_create(s_root);
        lv_obj_set_size(b, 148, TAB_H);
        lv_obj_set_pos(b, 8 + t * 156, TAB_Y);
        lv_obj_set_style_radius(b, 20, 0);
        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, kTabNames[t]);
        lv_obj_set_style_text_color(l, th_ink(), 0);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, on_tab, LV_EVENT_CLICKED, (void*)(intptr_t)t);
        s_tab_btn[t] = b;
        s_page[t] = make_page(s_root);
    }

    // Stepper rows, each on its group's page, in BOUNDS[] order.
    int y[3] = {0, 0, 0};
    int ci = 0;
    for (size_t i = 0; i < BOUNDS_N; i++) {
        const Bound* b = &BOUNDS[i];
        const int g = (int)b->group;
        lv_obj_t* page = s_page[g];

        if (b->widget == Widget::Preset) {
            // sampleinterval: stepping to an hour at 10 s a tap is absurd, so
            // it gets fixed chips. Every chip value sits inside the bound, so
            // Commands' clamp never silently turns a chip into another value.
            lv_obj_t* lbl = lv_label_create(page);
            lv_label_set_text(lbl, b->label);
            lv_obj_set_style_text_color(lbl, th_ink(), 0);
            lv_obj_set_pos(lbl, 8, y[g] + 10);
            for (int k = 0; k < kSampleN; k++) {
                lv_obj_t* chip = lv_button_create(page);
                lv_obj_set_size(chip, 58, 32);
                lv_obj_set_pos(chip, 150 + k * 64, y[g]);
                lv_obj_t* t = lv_label_create(chip);
                lv_obj_set_style_text_color(t, th_ink(), 0);
                lv_label_set_text(t, kSampleLbls[k]);
                lv_obj_center(t);
                s_chips[k] = ChipCtx{b->key, kSampleVals[k]};
                lv_obj_add_event_cb(chip, on_chip, LV_EVENT_CLICKED, &s_chips[k]);
                s_chip_obj[k] = chip;
            }
            y[g] += CHIP_ROW_H + 16;
            continue;
        }

        lv_obj_t* lbl = lv_label_create(page);
        lv_label_set_text(lbl, b->label);
        lv_obj_set_style_text_color(lbl, th_ink(), 0);
        lv_obj_set_pos(lbl, 8, y[g] + 16);

        s_val[i] = lv_label_create(page);
        lv_obj_set_style_text_color(s_val[i], th_ink2(), 0);
        lv_obj_set_pos(s_val[i], 200, y[g] + 16);

        for (int d = 0; d < 2; d++) {
            lv_obj_t* btn = lv_button_create(page);
            lv_obj_set_size(btn, 60, 48);
            lv_obj_set_pos(btn, d == 0 ? 330 : 400, y[g]);
            lv_obj_t* t = lv_label_create(btn);
            lv_label_set_text(t, d == 0 ? "-" : "+");
            lv_obj_center(t);
            s_ctx[ci] = StepCtx{b, d == 0 ? -1 : +1};
            lv_obj_add_event_cb(btn, on_step, LV_EVENT_CLICKED, &s_ctx[ci]);
            if (d == 0) s_minus[i] = btn; else s_plus[i] = btn;
            ci++;
        }
        y[g] += ROW_H;
    }

    for (int t = 0; t < 3; t++) {
        lv_obj_t* hint = lv_label_create(s_page[t]);
        lv_label_set_text(hint, kTabHints[t]);
        lv_obj_set_style_text_color(hint, th_muted(), 0);
        lv_obj_set_pos(hint, 8, y[t] + 12);
    }

    // Banner and toast share the top slot. Created last so they sit above the
    // pages in z-order.
    s_banner = lv_label_create(s_root);
    lv_obj_set_pos(s_banner, 8, BANNER_Y);
    lv_obj_set_width(s_banner, 464);
    lv_obj_set_style_bg_color(s_banner, th_warn(), 0);
    lv_obj_set_style_bg_opa(s_banner, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_banner, th_page(), 0);
    lv_obj_set_style_pad_all(s_banner, 4, 0);
    lv_label_set_text(s_banner, "");
    lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);

    s_toast = lv_label_create(s_root);
    lv_obj_set_pos(s_toast, 8, BANNER_Y);
    lv_obj_set_width(s_toast, 464);
    lv_obj_set_style_bg_color(s_toast, th_crit(), 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_toast, th_ink(), 0);
    lv_obj_set_style_pad_all(s_toast, 4, 0);
    lv_label_set_text(s_toast, "");
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);

    show_tab(0);
    screen_settings_refresh();
    return s_root;
}

void screen_settings_refresh(void) {
    if (!s_root) return;
    const CoolerState& st = cooler_state();
    Commands& cmd = panel_commands();
    char buf[48];

    // Nothing authoritative to edit from until the first /data arrives.
    const bool ready = st.valid;
    if (!ready) {
        lv_label_set_text(s_banner, "waiting for data - settings read-only");
        lv_obj_remove_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
    } else if (cooler_in_override(st)) {
        lv_label_set_text(s_banner, "override: fixed 3-5 C in force, set point waits");
        lv_obj_remove_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
    }

    for (size_t i = 0; i < BOUNDS_N; i++) {
        const Bound* b = &BOUNDS[i];
        if (b->widget != Widget::Stepper || !s_val[i]) continue;
        int v = cmd.display_value(b->key, st);
        bool pending = cmd.is_pending(b->key);
        std::snprintf(buf, sizeof(buf), "%d %s%s", v, b->unit, pending ? " ..." : "");
        lv_label_set_text(s_val[i], buf);
        lv_obj_set_style_text_opa(s_val[i], (pending || !ready) ? LV_OPA_50 : LV_OPA_COVER, 0);
        if (ready) {
            lv_obj_remove_state(s_minus[i], LV_STATE_DISABLED);
            lv_obj_remove_state(s_plus[i], LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(s_minus[i], LV_STATE_DISABLED);
            lv_obj_add_state(s_plus[i], LV_STATE_DISABLED);
        }
    }

    for (int i = 0; i < kSampleN; i++) {
        int cur = cmd.display_value(s_chips[i].key, st);
        lv_obj_set_style_bg_color(s_chip_obj[i], cur == s_chips[i].value ? th_good() : th_surface(), 0);
        if (ready) lv_obj_remove_state(s_chip_obj[i], LV_STATE_DISABLED);
        else       lv_obj_add_state(s_chip_obj[i], LV_STATE_DISABLED);
    }

    char toast_buf[64];
    if (cmd.take_toast(toast_buf, sizeof(toast_buf))) {
        std::strncpy(s_toast_msg, toast_buf, sizeof(s_toast_msg) - 1);
        s_toast_msg[sizeof(s_toast_msg) - 1] = 0;
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

void screen_settings_show_tab(int tab) {
    if (!s_root || tab < 0 || tab > 2) return;
    show_tab(tab);
    screen_settings_refresh();
}
