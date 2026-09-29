#include "backlight.h"
#include <Arduino.h>
#include <lvgl.h>
#include <time.h>
#include "display_gfx.h"

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

void backlight_cycle() {
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
        uint32_t last = display_last_touch_ms();
        bool awake = (last != 0 && millis() - last < 30000) ||
                     (s_manual_ms != 0 && millis() - s_manual_ms < 30000);
        uint8_t want = (!night || awake) ? user : (user < 50 ? user : 50);
        apply(want);
    }, 1000, nullptr);
}
