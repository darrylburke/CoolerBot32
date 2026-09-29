#include "screen_alarm.h"
#include "alarm.h"
#include "cooler_state.h"
#include "app.h"
#include "platform.h"
#include "theme.h"
#include <cstdio>
#include <cstring>

// Full-screen takeover: hidden whenever no condition is active, and raised
// above everything else (including whatever Task 13's tileview later adds)
// the moment one is. Parented directly to the nav root rather than into any
// page/tile -- an alarm must interrupt the operator regardless of which
// screen they're currently looking at.
static lv_obj_t* s_root;
static lv_obj_t* s_title;
static lv_obj_t* s_detail;
static AlarmId s_showing = AlarmId::None;

static void on_ack(lv_event_t*) {
    panel_alarms().acknowledge(s_showing, platform_epoch_utc());
    screen_alarm_refresh();
}

lv_obj_t* screen_alarm_create(lv_obj_t* parent) {
    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, 480, 480);
    // th_crit() is the theme's critical/red token (see screen_settings.cpp's
    // failure toast) -- reused here rather than inventing a bespoke dark red.
    lv_obj_set_style_bg_color(s_root, th_crit(), 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    // Titles like "AC NOT RESPONDING" are wider than the panel at 48 px, so
    // the title wraps inside a fixed width instead of running off both edges.
    s_title = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_title, th_ink(), 0);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_title, 440);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 50);

    s_detail = lv_label_create(s_root);
    lv_obj_set_style_text_color(s_detail, th_ink(), 0);
    lv_obj_set_style_text_align(s_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_detail, 420);
    lv_obj_align(s_detail, LV_ALIGN_CENTER, 0, 30);

    lv_obj_t* btn = lv_button_create(s_root);
    lv_obj_set_size(btn, 240, 64);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_bg_color(btn, th_surface(), 0);
    lv_obj_t* t = lv_label_create(btn);
    lv_label_set_text(t, "ACKNOWLEDGE");
    lv_obj_set_style_text_color(t, th_ink(), 0);
    lv_obj_center(t);
    lv_obj_add_event_cb(btn, on_ack, LV_EVENT_CLICKED, nullptr);
    return s_root;
}

void screen_alarm_refresh(void) {
    if (!s_root) return;
    const CoolerState& st = cooler_state();
    AlarmId id = panel_alarms().active(platform_epoch_utc());
    s_showing = id;
    if (id == AlarmId::None) {
        lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_root);
    lv_label_set_text(s_title, Alarms::text(id));

    char buf[160];
    switch (id) {
        case AlarmId::ControllerSilent:
            std::snprintf(buf, sizeof(buf), "no message for %d min",
                          (int)((platform_epoch_utc() - st.last_rx_epoch) / 60));
            break;
        case AlarmId::NoResponse:
            std::snprintf(buf, sizeof(buf),
                          "cooling requested for 10 min, compressor not running.\n"
                          "Check AC power, mode, relay and CN3 wiring.");
            break;
        case AlarmId::BoxSensorFault:
            std::snprintf(buf, sizeof(buf), "%s",
                          std::strcmp(st.mode, "blind") == 0 ? "cooling on timer only"
                          : std::strstr(st.mode, "proxy")    ? "cooling on fin sensor"
                                                              : "box temperature lost");
            break;
        case AlarmId::FinSensorFault:
            std::snprintf(buf, sizeof(buf), "icing protection on timer backstop");
            break;
        case AlarmId::NotKeepingUp:
            if (cooler_in_override(st))
                std::snprintf(buf, sizeof(buf), "%.1f vs override 5", (double)st.temp);
            else
                std::snprintf(buf, sizeof(buf), "%.1f vs set %d", (double)st.temp, st.coolerset);
            break;
        default:
            std::snprintf(buf, sizeof(buf), "mode %s", st.mode);
            break;
    }
    lv_label_set_text(s_detail, buf);
}
