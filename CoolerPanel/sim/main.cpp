#include "lvgl.h"
#include "platform.h"
#include "screenshot.h"
#include "theme.h"
#include "ui.h"
#include "app.h"
#include "screen_config.h"
#include "config.h"
#include "nav.h"
#include "device_config.h"
#include <SDL2/SDL.h>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <thread>

static std::chrono::steady_clock::time_point g_start;
extern "C" uint32_t platform_now_ms(void) {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - g_start).count();
}
extern "C" int64_t platform_epoch_utc(void) {
    using namespace std::chrono;
    return (int64_t)duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}
extern "C" void platform_delay_ms(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

extern void mqtt_poll();

int main(int argc, char** argv) {
    int frames = 0; const char* shot = nullptr;
    const char* screen = nullptr; // debug flag: --screen boot|setup|setup-qr
    bool seed = false;            // debug flag: --seed-history (see seed_history.cpp)
    const char* fixture = nullptr; // debug flag: --fixture FILE -- feed one /data
                                   // payload instead of connecting to the broker
    int page = -1;                // debug flag: --page 0|1|2 (Trend|Settings|Detail)
    int tab = -1;                 // debug flag: --tab 0|1|2 (Settings: Box|Coil|Timing)
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--screen") && i + 1 < argc) screen = argv[++i];
        else if (!strcmp(argv[i], "--seed-history")) seed = true;
        else if (!strcmp(argv[i], "--fixture") && i + 1 < argc) fixture = argv[++i];
        else if (!strcmp(argv[i], "--page") && i + 1 < argc) page = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tab") && i + 1 < argc) tab = atoi(argv[++i]);
    }
    g_start = std::chrono::steady_clock::now();
    lv_init();
    lv_tick_set_cb(platform_now_ms);
    lv_display_t* disp = lv_sdl_window_create(480, 480);
    (void)disp;
    lv_indev_t* mouse = lv_sdl_mouse_create();
    // display_design.md §6: "long-press (>= 800 ms)" -- LVGL's default
    // (LV_INDEV_DEF_LONG_PRESS_TIME, lv_indev.c) is 400 ms, so raise it here.
    lv_indev_set_long_press_time(mouse, 800);

    // v9.3.0 signature is lv_result_t lv_freetype_init(uint32_t max_glyph_cnt);
    // (the single-argument form -- see task-5-report.md for the header grep).
    lv_freetype_init(256);
    extern ThemeFonts load_fonts(const char* dir);
    ThemeFonts fonts = load_fonts("assets/ttf");
    theme_set_fonts(fonts);
    // Cooler32 branding: the wordmark and the NorthTrail creator badge live in
    // assets/brand/. logo_load returns nullptr if a file is missing, and
    // boot_build degrades gracefully, so a missing asset is cosmetic only.
    extern const lv_image_dsc_t* logo_load(const char* path);
    extern lv_obj_t* boot_build(lv_obj_t*, const lv_image_dsc_t*, const lv_image_dsc_t*);
    const lv_image_dsc_t* logo = logo_load("mockups/northtrail_logo.png");
    const lv_image_dsc_t* wordmark = logo_load("assets/brand/cooler32-wordmark-dark.png");

    bool headless = shot || frames > 0;
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, th_page(), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    if (headless && screen && !strcmp(screen, "boot")) {
        // Snapshot the boot splash in isolation; skip the (near-empty) UI
        // and MQTT machinery entirely since boot_build doesn't consume it.
        boot_build(scr, wordmark, logo);
    } else if (headless && screen && (!strcmp(screen, "setup") || !strcmp(screen, "setup-qr"))) {
        // Setup-mode (captive portal) screen with representative dummy
        // credentials -- same shared builder the device portal uses.
        // "setup-qr" additionally opens the join-Wi-Fi QR overlay, as if
        // the QR button had been tapped.
        extern void setup_build(lv_obj_t*, const char*, const char*);
        extern void setup_show_wifi_qr();
        setup_build(scr, "CoolerPanel-Setup-A1B2", "kx7mQ9pR2v");
        if (!strcmp(screen, "setup-qr")) setup_show_wifi_qr();
    } else {
        // Panel-local config (Task 16): load before ui_init() so its
        // apply-at-startup calls (panel_alarms().configure/trend_set_zoom)
        // see the persisted value, not the struct defaults.
        panel_config() = panel_cfg_load();

        // The simulator has no persisted DeviceConfig/NVS -- unlike the
        // device (config_nvs.cpp), it never boots "unconfigured". Treat it
        // as always configured so a normal run goes straight from the
        // splash to the dashboard; the only way to see the live setup
        // screen (short of the --screen debug flag above) is tapping the
        // splash's SETUP gear within its visible window.
        DeviceConfig cfg;
        cfg.configured = true;
        // Simulator saver: write the form's values back to the config file the
    // sim already loads from, so a Save survives a restart just as NVS would
    // on the device.
    screen_config_set_saver([](const DeviceConfig& c) -> bool {
        extern bool sim_save_device_config(const DeviceConfig&);
        return sim_save_device_config(c);
    });

    UiOutcome outcome = ui_init(cfg, wordmark, logo,
                                     "CoolerPanel-Setup-A1B2", "kx7mQ9pR2v");

        if (outcome == UiOutcome::Normal) {
            // Seed BEFORE connecting: the live samples that follow then land
            // on the end of the synthetic week rather than being wiped by it.
            if (seed) {
                extern void seed_demo_history(int, int, int);
                extern void screen_trend_refresh(void);
                seed_demo_history(5, 10, 80);   // setpoint 5C, maxrun 10m, duty 80%
                screen_trend_refresh();
            }
            if (fixture) {
                // Deterministic screenshots: route one captured /data (plus an
                // "online" availability) through the same entry point the
                // broker would use, and never connect.
                extern bool sim_feed_fixture(const char* path);
                if (!sim_feed_fixture(fixture)) {
                    fprintf(stderr, "cannot read fixture %s\n", fixture);
                    return 2;
                }
            } else {
                extern void mqtt_start(const SimConfig&);
                SimConfig scfg = config_load(argc, argv);
                mqtt_start(scfg);   // connects in the background; the main loop
                                     // below already polls mqtt_poll() every tick.
            }
            if (page >= 0) nav_show_page(page);
            if (tab >= 0) {
                extern void screen_settings_show_tab(int);
                screen_settings_show_tab(tab);
            }
            ui_refresh();
        }
    }

    int painted = 0;
    while (true) {
        lv_timer_handler();
        mqtt_poll();   // no-op unless mqtt_start() ran (interactive mode); drives
                        // the mosquitto network loop on this (main/UI) thread.
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (frames && ++painted >= frames) break;
    }
    if (shot) {
        if (!screenshot_save(shot)) { fprintf(stderr, "screenshot failed\n"); return 2; }
        printf("wrote %s\n", shot);
    }
    return 0;
}
