#include "ui.h"
#include "lvgl.h"
#include "theme.h"
#include "nav.h"
#include "app.h"
#include "commands.h"   // panel_commands() is only forward-declared in app.h;
                         // .tick() below needs the complete Commands type.
#include "alarm.h"      // ditto for panel_alarms()/.update() below.
#include "panel_config.h"  // ditto for panel_config().alarm/.default_zoom_s.
#include "screen_trend.h"  // trend_set_zoom() -- applying the persisted default.
#include "platform.h"
#include "device_config.h"

// The LLMMon dashboard (screen_dashboard.cpp) and its long-press -> system
// status handler are gone with app_model. Task 7 replaced the near-empty
// stub with the Trend screen; Task 13 replaced that single screen with
// nav_init()'s three-page tileview (Trend | Settings | Detail) plus the
// alarm takeover above it. Task 17 restores the boot splash -> setup-or-main
// flow LLMMon's device/sim main()s used to drive by hand (see
// ~/projects/LLMMon/firmware/device/src/main.cpp): it now lives here so both
// platforms share one decision instead of re-deriving it.
extern lv_obj_t* boot_build(lv_obj_t*, const lv_image_dsc_t*, const lv_image_dsc_t*);
extern bool boot_setup_requested();
extern void setup_build(lv_obj_t*, const char*, const char*);

namespace {
// How long the splash stays up watching for a SETUP-gear tap before this
// function decides Normal-vs-Setup on `cfg.configured` alone. This is the
// whole visible boot -> decision window, not a network-connect wait
// (that's the caller's job -- WiFi/MQTT bring-up is platform-specific and
// has no business in shared/ui).
constexpr uint32_t kSplashMs = 4500;   // 4.5s: long enough to actually read
                                       // the splash and hit the SETUP gear
}

// Trend/Settings/Detail values only change on an incoming /data message
// (every ~30 s, or on a relay-state change), so a display-refresh cadence
// far below the old LLMMon dashboard's 10 Hz tick is plenty responsive here
// -- 1 Hz keeps the screens current without redoing the chart's
// downsample/redraw ten times a second for data that isn't moving.
//
// Commands::tick() is different: it must run at this cadence regardless of
// whether /data has arrived, since it is what flushes a debounced request
// after CMD_DEBOUNCE_MS and expires an unacknowledged one after
// CMD_DEADLINE_MS (Task 9) -- both are wall-clock deadlines, not events.
//
// panel_alarms().update() is called from here for the same reason, and it is
// NOT redundant with the call app_on_mqtt_message() already makes on every
// /data message: ControllerSilent is defined by the ABSENCE of a message, so
// it can only ever be detected by the passage of time between ticks, never
// from inside the handler that reacts to a message arriving. Skipping this
// call would mean a cooler that stops publishing altogether never alarms
// until the next unrelated message happens to arrive -- which for a truly
// silent controller is never.
//
// Both calls must survive nav_init()/nav_refresh() taking over screen
// creation/refresh below -- neither is a screen concern, so nav.cpp has no
// reason to know about them, but dropping either from this tick would be a
// silent regression (nothing publishes; ControllerSilent can never fire).
static void tick_cb(lv_timer_t*) {
    panel_commands().tick(platform_now_ms());
    panel_alarms().update(cooler_state(), platform_epoch_utc());
    ui_refresh();
}

static DeviceConfig s_cfg;
const DeviceConfig& ui_device_config() { return s_cfg; }

UiOutcome ui_init(const DeviceConfig& cfg, const lv_image_dsc_t* wordmark,
                   const lv_image_dsc_t* logo, const char* ap_ssid,
                   const char* ap_pass) {
    s_cfg = cfg;
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, th_page(), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Splash first (display_design.md §5.1): wordmark + creator logo, with a
    // SETUP gear the operator can tap to force the setup screen even on an
    // already-configured panel. boot_build() clears its own "tapped" latch
    // on every call, so this poll only ever sees a tap that happened during
    // *this* splash.
    lv_obj_t* boot_scr = boot_build(scr, wordmark, logo);
    uint32_t t0 = platform_now_ms();
    while (platform_now_ms() - t0 < kSplashMs && !boot_setup_requested()) {
        lv_timer_handler();
        // Yield rather than spin: on the device, platform_delay_ms() ->
        // delay() actually yields to the RTOS scheduler, servicing the
        // idle-task watchdog and background tasks, instead of holding
        // core 1 in a tight loop for the whole splash window. 5 ms matches
        // the polling cadence both main loops (device/sim) already use
        // elsewhere.
        platform_delay_ms(5);
    }

    if (boot_setup_requested() || !cfg.configured) {
        lv_obj_delete(boot_scr);
        setup_build(scr, ap_ssid, ap_pass);
        return UiOutcome::Setup;
    }

    app_init_history();
    nav_init(scr);   // builds the tileview (Trend/Settings/Detail) and the
                      // alarm takeover. nav_init() is idempotent (Task 13's
                      // review flagged a double-call leak), and ui_init()
                      // only ever reaches this line once per invocation, but
                      // the guard stays belt-and-braces regardless.
    lv_obj_delete(boot_scr);

    // Apply the panel-local config (Task 16). The platform main() is
    // expected to have already overwritten panel_config() with the
    // persisted value (panel_cfg_load()) before calling ui_init(); if it
    // didn't, this just applies the struct defaults, which is safe.
    panel_alarms().configure(panel_config().alarm);
    trend_set_zoom(panel_config().default_zoom_s);

    lv_timer_create(tick_cb, 1000, nullptr);   // 1 Hz
    return UiOutcome::Normal;
}

void ui_refresh() {
    nav_refresh();
}
