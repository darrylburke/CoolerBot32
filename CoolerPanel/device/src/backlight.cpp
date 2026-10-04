#include "backlight.h"
#include <Arduino.h>
#include <lvgl.h>
#include <time.h>
#include "display_gfx.h"
#include "backlight_policy.h"
#include "app.h"
#include "alarm.h"
#include "platform.h"

static const uint8_t kLevels[] = {255, 128, 40};   // 100% / 50% / 15% -- the
                                                    // physical-button cycle's
                                                    // three presets.
static int s_level_idx = 2;   // which preset the NEXT button press lands on;
                               // unrelated to s_user below (Task 14: the
                               // configured value, not a button press,
                               // decides the level actually shown at boot).
static uint8_t s_user = 255;       // current daytime ("user") level, 0..255
static bool s_night_dim = true;    // Task 16's PanelConfig::night_dim
static uint32_t s_manual_ms = 0;   // last button change counts as activity
static uint32_t s_boot_ms = 0;     // so does boot: the panel starts bright
static uint8_t s_cur = 255;

static void apply(uint8_t want) {
    if (want != s_cur) {
        s_cur = want;
        display_set_brightness(want);
    }
}

// PanelConfig::backlight is a percent, 5..100 (already clamped by
// panel_config_from_json); display_set_brightness() wants 0..255.
static uint8_t pct_to_255(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return (uint8_t)((pct * 255 + 50) / 100);
}

bool backlight_is_off() { return s_cur == 0; }

void backlight_cycle() {
    // A press on a dark screen only wakes it; the next press cycles.
    if (s_cur == 0) { s_manual_ms = millis(); return; }
    s_level_idx = (s_level_idx + 1) % (int)(sizeof kLevels / sizeof kLevels[0]);
    s_user = kLevels[s_level_idx];
    s_manual_ms = millis();
    apply(s_user);
    Serial.printf("backlight: level %d/3 (%u)\n", s_level_idx + 1, s_user);
}

void backlight_apply_config(int percent, bool night_dim) {
    s_user = pct_to_255(percent);
    s_night_dim = night_dim;
    apply(s_user);
    Serial.printf("backlight: applying configured %d%% (%u/255), night_dim=%s\n",
                  percent, s_user, night_dim ? "on" : "off");
}

void backlight_init() {
    s_boot_ms = millis();
    apply(s_user);   // don't wait for the first 1 s tick
    lv_timer_create([](lv_timer_t*) {
        uint8_t user = s_user;
        bool night = false;
        if (s_night_dim) {
            time_t t = time(nullptr);
            if (t > 1600000000) {                 // only once NTP has synced
                struct tm lt;
                localtime_r(&t, &lt);             // TZ set by ntp_begin
                night = (lt.tm_hour >= 23 || lt.tm_hour < 7);
            }
        }
        // Time since the most recent activity: boot, a touch, or the button.
        const uint32_t now = millis();
        uint32_t idle = now - s_boot_ms;
        const uint32_t touch = display_last_touch_ms();
        if (touch != 0 && now - touch < idle) idle = now - touch;
        if (s_manual_ms != 0 && now - s_manual_ms < idle) idle = now - s_manual_ms;
        const bool alarm = panel_alarms().active(platform_epoch_utc()) != AlarmId::None;
        const uint8_t want = backlight_level(user, night, idle, alarm);
        apply(want);
    }, 250, nullptr);   // 250 ms: a touch on a dark screen lights it at once
}
