#include <Arduino.h>
#include <time.h>
#include <lvgl.h>
#include "platform.h"
#include "display_gfx.h"
#include "device_fonts.h"
#include "theme.h"
#include "ui.h"
#include "app.h"
#include "history.h"     // History::capacity() -- app.h only forward-declares it
#include "panel_config.h"
#include "config_nvs.h"
#include "screen_config.h"
#include "net_wifi.h"
#include "ntp.h"
#include "mqtt_pubsub.h"
#include "mqtt_router.h"
#include "portal.h"
#include "backlight.h"
#include "buttons.h"

// platform.h seam (shared/ links against these).
extern "C" uint32_t platform_now_ms(void) { return millis(); }
extern "C" int64_t  platform_epoch_utc(void) { return (int64_t)time(nullptr); }
// Arduino-ESP32's delay() yields to the RTOS scheduler (services the idle-task
// watchdog and background tasks) rather than spinning -- unlike millis()/
// time() above, this one actually matters for *how* it waits, not just what
// it returns; see shared/ui/ui.cpp's boot-splash loop (Task 17 fix-round).
extern "C" void platform_delay_ms(uint32_t ms) { delay(ms); }

extern const lv_image_dsc_t cooler32_wordmark;
extern const lv_image_dsc_t northtrail_logo;

static DeviceConfig g_cfg;

// Task 14 fix round: sustained-WiFi-down watchdog.
//
// Nothing reads app_link_state() anywhere in shared/ or device/ -- it is
// set (by mqtt_pubsub.cpp) but never displayed, so a plain "show link-down
// on the dashboard" fix would still be invisible to the user. Worse, a
// REBOOT with stale/wrong WiFi credentials (router password changed,
// router replaced, panel physically moved, or just a power blip after any
// of those) never gets far enough to set CoolerState::valid or
// availability_seen -- both ControllerOffline and ControllerSilent are
// gated on those flags, so on that path NEITHER ALARM CAN EVER FIRE. The
// dashboard would sit showing blank data forever with no indication
// anything is wrong, and the only way out (the splash's SETUP gear) closes
// after 1.5 s at boot, long before anyone notices a food-storage monitor
// has gone dark.
//
// Fix: track how long WiFi has been continuously disassociated and, past a
// threshold, stage a portal reboot via the same config_request_portal()
// mechanism the touch-hold gesture already uses -- next boot's
// config_portal_boot() (read into force_portal below) takes it straight
// into Setup through the existing path, so there is no new decision logic
// and shared/ui is untouched.
//
// Deliberately watches WiFi ASSOCIATION (wifi_is_connected()), NOT broker
// reachability: a broker outage must never trigger this, since
// reprovisioning WiFi would do nothing for a dead/unreachable broker and
// would just strand the user off their real network for no benefit. This
// is why the watchdog lives here, calling wifi_is_connected() itself,
// rather than reusing mqtt_pubsub.cpp's app_set_link_state(LINK_WIFI_DOWN)
// signal -- that call site is correct today but is one refactor away from
// also firing on other conditions, and conflating the two would risk
// exactly the failure mode this fix exists to prevent.
//
// 10 minutes: long enough to ride out a roaming blip, an AP reboot, or a
// transient RF glitch without bouncing a panel that would have recovered
// on its own; short enough that a genuinely wrong/stale credential is
// caught well within a single shift rather than sitting silently for
// hours. No reboot loop: once this fires, config_request_portal() reboots
// straight into Setup, and loop() (where this watchdog runs) is never
// reached again on that path -- portal_run() below takes over completely
// and never returns, so the watchdog cannot re-arm and re-trigger itself.
static constexpr uint32_t kWifiDownRebootMs = 10 * 60 * 1000;
static uint32_t s_wifi_down_since_ms = 0;   // 0 == currently associated

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("Cooler panel device boot. PSRAM: %u bytes\n", (unsigned)ESP.getPsramSize());

    // All NVS writes happen before display_init() (see config_nvs.h).
    config_apply_pending();
    g_cfg = config_load_or_seed();
    std::string ap_pass = config_get_ap_pass();   // may write NVS on first use

    display_init();
    device_fonts_init();

    // Panel-local config (Task 16): load before ui_init() so its
    // apply-at-startup calls (panel_alarms().configure/trend_set_zoom) see
    // the persisted value, not the struct defaults. panel_cfg_load() only
    // reads NVS, so (unlike config_save/_clear) it carries none of the
    // flash-write hazard that confines DeviceConfig writes to before
    // display_init() -- safe to call here, after it.
    panel_config() = panel_cfg_load();

    // Task 14 step 3: a reviewer caught that nothing applied the persisted
    // backlight/night_dim Task 16 stores and clamps. Apply it now that
    // display_init() has brought the PWM channel up, and after
    // panel_cfg_load() per the brief.
    backlight_init();
    backlight_apply_config(panel_config().backlight, panel_config().night_dim);

    buttons_init();

    // Boot decision (spec §4). Ways into setup: the splash's own SETUP gear
    // (handled entirely inside ui_init() -- see shared/ui/ui.cpp), the
    // touch-hold gesture (soft resets only -- a cold power-up baselines a
    // resting finger away, so this can't false-trigger there), and a staged
    // portal request from a previous session. Rather than re-deriving
    // ui_init()'s Setup-vs-Normal decision here, fold both device-specific
    // reasons into a local copy of the config with `configured` forced
    // false: ui_init() takes exactly the same Setup branch it would for a
    // never-configured panel, so the splash/decision flow stays the single
    // shared one Task 17 built instead of a second device-side copy of it.
    // `g_cfg` itself (the real stored credentials, passed to portal_run()/
    // wifi_begin()/mqtt_begin() below) is untouched.
    bool force_portal = config_portal_boot();
    bool touch_held = display_touch_held(1500);   // must run before any
                                                   // lv_timer_handler() call
                                                   // starts pumping the
                                                   // input driver -- see
                                                   // display_gfx.h.
    if (touch_held) Serial.println("boot: touch held -- forcing setup");
    DeviceConfig ui_cfg = g_cfg;
    if (force_portal || touch_held) ui_cfg.configured = false;

    // Same efuse-derived suffix portal_run() (device-only, below) uses for
    // its real SoftAP, so the setup screen ui_init() may draw here already
    // shows the SSID the captive portal will actually come up as a moment
    // later instead of a placeholder that would visibly change underneath
    // the user.
    char suffix[8];
    snprintf(suffix, sizeof suffix, "%04X", (uint16_t)(ESP.getEfuseMac() >> 32));
    std::string ap_ssid = std::string("CoolerPanel-Setup-") + suffix;

    // Device saver: stage into NVS and reboot, which is the same path the
    // captive portal's Save uses -- the panel comes back up on the new
    // credentials rather than trying to re-associate live.
    screen_config_set_saver([](const DeviceConfig& c) -> bool {
        config_save(c);
        delay(200);
        ESP.restart();
        return true;   // not reached
    });

    UiOutcome outcome = ui_init(ui_cfg, &cooler32_wordmark, &northtrail_logo,
                                 ap_ssid.c_str(), ap_pass.c_str());

    if (outcome == UiOutcome::Setup) {
        portal_run(g_cfg, ap_pass);              // never returns
    }

    // Normal mode: ui_init() already built and revealed the dashboard, so
    // there is no splash left to hide networking behind -- bring WiFi/NTP/
    // MQTT up now, in the background. wifi_begin()/mqtt_begin() are both
    // non-blocking; loop() below keeps polling both to completion and
    // through every future reconnect.
    Serial.printf("history: capacity=%u samples\n",
                  (unsigned)panel_history().capacity());
    ntp_begin();
    wifi_begin(g_cfg);
    router_set_prefix(g_cfg.mqtt_base.c_str());
    mqtt_begin(g_cfg);
}

void loop() {
    lv_timer_handler();
    mqtt_poll();
    buttons_poll();

    // Sustained-WiFi-down watchdog -- see kWifiDownRebootMs above for the
    // full reasoning. Resets the instant WiFi re-associates; only stages a
    // reboot once WiFi has been down continuously for the whole threshold.
    if (wifi_is_connected()) {
        s_wifi_down_since_ms = 0;
    } else {
        if (s_wifi_down_since_ms == 0) {
            s_wifi_down_since_ms = millis();
        } else if (millis() - s_wifi_down_since_ms >= kWifiDownRebootMs) {
            Serial.println("wifi: down for 10+ min, staging portal reboot");
            config_request_portal();   // never returns
        }
    }

    delay(5);
}
