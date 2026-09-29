#include "screen_detail.h"
#include "cooler_state.h"
#include "commands.h"
#include "status_text.h"
#include "app.h"
#include "screen_config.h"
#include "platform.h"
#include "theme.h"
#include <cstdio>

// Diagnostics for troubleshooting without a laptop at the AC: every run-state
// field /data carries, plus the one maintenance task the panel can drive --
// fin thermistor calibration (spec §2.7).
//
// Reset-to-defaults throws away a calibration that took a whole pull-down to
// collect, so it takes two taps: the first arms it for CONFIRM_MS, the second
// sends it.
#define CAL_Y       296
#define CONFIRM_MS  5000
#define FAIL_MS     4000   // how long a failed-send notice replaces the cal line

static lv_obj_t* s_root;
static lv_obj_t* s_body;
static lv_obj_t* s_cal_lbl;
static lv_obj_t* s_cal_btn;
static lv_obj_t* s_cal_btn_lbl;
static lv_obj_t* s_reset_btn;
static lv_obj_t* s_reset_lbl;
static bool s_reset_armed = false;
static uint32_t s_reset_armed_ms = 0;
// Aborting throws away hours of collected points, so it arms like Reset.
static bool s_abort_armed = false;
static uint32_t s_abort_armed_ms = 0;
// Commands' failure toast is shown by the Settings page, which is not the one
// on screen here -- so a failed action is also flagged locally.
static bool s_fail = false;
static uint32_t s_fail_ms = 0;

static void send(const char* key, int value) {
    if (!panel_commands().action(key, value)) { s_fail = true; s_fail_ms = platform_now_ms(); }
}

static void on_cal(lv_event_t*) {
    const CoolerState& st = cooler_state();
    const uint32_t now = platform_now_ms();
    if (!st.cal_active) {
        send("calibrate", 1);
    } else if (s_abort_armed && now - s_abort_armed_ms < CONFIRM_MS) {
        send("calibrate", 0);
        s_abort_armed = false;
    } else {
        s_abort_armed = true;
        s_abort_armed_ms = now;
    }
    screen_detail_refresh();
}

static void on_reset(lv_event_t*) {
    const uint32_t now = platform_now_ms();
    if (s_reset_armed && now - s_reset_armed_ms < CONFIRM_MS) {
        send("fincal_reset", 1);
        s_reset_armed = false;
    } else {
        s_reset_armed = true;
        s_reset_armed_ms = now;
    }
    screen_detail_refresh();
}

static lv_obj_t* small_button(lv_obj_t* parent, int x, int w, lv_obj_t** lbl, lv_event_cb_t cb) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_size(b, w, 44);
    lv_obj_set_pos(b, x, CAL_Y + 32);
    lv_obj_set_style_bg_color(b, th_surface(), 0);
    *lbl = lv_label_create(b);
    lv_obj_set_style_text_color(*lbl, th_ink(), 0);
    lv_obj_center(*lbl);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
}

lv_obj_t* screen_detail_create(lv_obj_t* parent) {
    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, 480, 480);
    lv_obj_set_style_bg_color(s_root, th_page(), 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    s_body = lv_label_create(s_root);
    lv_obj_set_style_text_color(s_body, th_ink(), 0);
    lv_obj_set_pos(s_body, 12, 12);
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_body, 456);

    s_cal_lbl = lv_label_create(s_root);
    lv_obj_set_style_text_color(s_cal_lbl, th_ink2(), 0);
    lv_obj_set_pos(s_cal_lbl, 12, CAL_Y);
    s_cal_btn = small_button(s_root, 12, 200, &s_cal_btn_lbl, on_cal);
    s_reset_btn = small_button(s_root, 224, 220, &s_reset_lbl, on_reset);

    // Entry point to the WiFi/MQTT form. This page is where you come when
    // something looks wrong, so it is where the "change the broker" door
    // belongs -- not buried behind a 4.5s boot-splash gear tap.
    lv_obj_t* cfg = lv_button_create(s_root);
    lv_obj_set_size(cfg, 220, 46);
    lv_obj_align(cfg, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_set_style_bg_color(cfg, th_surface(), 0);
    lv_obj_t* cl = lv_label_create(cfg);
    lv_label_set_text(cl, "WiFi / MQTT setup");
    lv_obj_center(cl);
    lv_obj_add_event_cb(cfg, [](lv_event_t*) { screen_config_open(); },
                        LV_EVENT_CLICKED, nullptr);

    screen_detail_refresh();
    return s_root;
}

void screen_detail_refresh(void) {
    if (!s_root) return;
    const CoolerState& s = cooler_state();
    char state[32], hold[16], up[16], coil[48], cal[48], buf[640];

    fmt_state_value(s, state, sizeof(state));
    fmt_dur(s.hold_s, hold, sizeof(hold));
    fmt_dur(s.uptime_s, up, sizeof(up));
    char slope[16];
    if (s.fin_slope_valid) std::snprintf(slope, sizeof(slope), "%+.1f C/min", (double)s.fin_slope);
    else                   std::snprintf(slope, sizeof(slope), "-- C/min");   // first 60 s
    if (s.fin_temp_valid)
        std::snprintf(coil, sizeof(coil), "%.1f C  %.0f ohm  %s",
                      (double)s.fin_temp, (double)s.fin_ohms, slope);
    else
        // "sensor fault" only once the controller has declared it (10 s debounce).
        std::snprintf(coil, sizeof(coil), s.fin_fault ? "-- (sensor fault)" : "--");
    if (s.fin_cal_err_valid)
        std::snprintf(cal, sizeof(cal), "%s  beta %.0f  r0 %.0f  err %.2f C",
                      s.fin_cal ? "ok" : "default", (double)s.fin_beta, (double)s.fin_r0,
                      (double)s.fin_cal_err);
    else
        std::snprintf(cal, sizeof(cal), "%s  beta %.0f  r0 %.0f",
                      s.fin_cal ? "ok" : "default", (double)s.fin_beta, (double)s.fin_r0);

    std::snprintf(buf, sizeof(buf),
        "mode      %s   override %s\n"
        "state     %s   hold %s\n"
        "relay %d   call %d   compressor %s\n"
        "coil      %s\n"
        "fin cal   %s\n"
        "faults    box %d   coil %d   no-response %d\n"
        "uptime    %s   link %s\n"
        "last rx   %llds ago",
        fmt_mode_label(s), s.override_src[0] ? s.override_src : "--",
        state, hold,
        s.relay, s.cool_call, fmt_compressor(s),
        coil,
        cal,
        s.sht_fault, s.fin_fault, s.no_response,
        up, s.online ? "online" : "OFFLINE",
        (long long)(platform_epoch_utc() - s.last_rx_epoch));
    lv_label_set_text(s_body, buf);

    // Calibration: progress while running, otherwise what a run needs.
    if (s_fail && platform_now_ms() - s_fail_ms >= FAIL_MS) s_fail = false;
    lv_obj_set_style_text_color(s_cal_lbl, s_fail ? th_crit() : th_ink2(), 0);
    if (s_fail)
        std::snprintf(buf, sizeof(buf), "Send failed - check the MQTT link");
    else if (s.cal_active)
        std::snprintf(buf, sizeof(buf), "Calibrating: %d points, span %.1f C (need 4 / 8 C)",
                      s.cal_points, (double)s.cal_span);
    else if (s.cal_result == CAL_RESULT_CALIBRATED)
        std::snprintf(buf, sizeof(buf), "Last run: calibrated, beta %.0f, err %.2f C",
                      (double)s.fin_beta, (double)s.fin_cal_err);
    else if (s.cal_result == CAL_RESULT_REJECTED)
        std::snprintf(buf, sizeof(buf), "Last run: fit rejected (err %.2f C), values kept",
                      (double)s.fin_cal_err);
    else if (s.cal_result == CAL_RESULT_NO_FIT)
        std::snprintf(buf, sizeof(buf), "Last run: ended without a fit, values kept");
    else
        std::snprintf(buf, sizeof(buf), "Fin calibration: empty box, cooling paused (RULES-v4)");
    lv_label_set_text(s_cal_lbl, buf);

    if (s_abort_armed && (!s.cal_active || platform_now_ms() - s_abort_armed_ms >= CONFIRM_MS))
        s_abort_armed = false;
    lv_label_set_text(s_cal_btn_lbl, !s.cal_active ? "Start calibration"
                                   : s_abort_armed ? "Tap again to abort" : "Abort calibration");
    lv_obj_set_style_bg_color(s_cal_btn, s_abort_armed ? th_crit()
                                       : s.cal_active ? th_warn() : th_surface(), 0);

    if (s_reset_armed && platform_now_ms() - s_reset_armed_ms >= CONFIRM_MS) s_reset_armed = false;
    lv_label_set_text(s_reset_lbl, s_reset_armed ? "Tap again to reset" : "Reset calibration");
    lv_obj_set_style_bg_color(s_reset_btn, s_reset_armed ? th_crit() : th_surface(), 0);

    // A command to an offline controller would be published and silently
    // dropped, so the controls wait for it to be online.
    if (s.valid && s.online) {
        lv_obj_remove_state(s_cal_btn, LV_STATE_DISABLED);
        lv_obj_remove_state(s_reset_btn, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(s_cal_btn, LV_STATE_DISABLED);
        lv_obj_add_state(s_reset_btn, LV_STATE_DISABLED);
    }
}
