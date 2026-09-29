#pragma once
#include "lvgl.h"

struct DeviceConfig;

// Boot -> setup-or-main flow (restored so it's shared between the device and
// the simulator, rather than each platform re-deriving it -- see
// config/device_config.cpp's decide_boot_mode() for the same policy in
// truth-table form).
//
// Builds the boot splash (screen_boot.cpp's boot_build(), with the wordmark
// and creator-logo images) and shows it briefly. If the splash's SETUP gear
// is tapped during that window, or `cfg.configured` is false, ui_init()
// builds the setup screen (screen_setup.cpp's setup_build(), with the given
// AP credentials -- the caller's captive portal, if any, is the source of
// truth for what those actually are; ui.cpp just displays them) and returns
// UiOutcome::Setup. Otherwise it builds the dashboard (nav_init()'s
// tileview), reveals it, and returns UiOutcome::Normal. Either way the
// caller uses the outcome to decide what to bring up next (e.g. start
// MQTT only for Normal; run a captive portal's networking only for Setup).
enum class UiOutcome { Normal, Setup };

// The config ui_init() was handed. screen_config reads it to populate its
// form; nothing else should need it.
const DeviceConfig& ui_device_config();

UiOutcome ui_init(const DeviceConfig& cfg, const lv_image_dsc_t* wordmark,
                   const lv_image_dsc_t* logo, const char* ap_ssid,
                   const char* ap_pass);
void ui_refresh();   // repaint from model (no-op / harmless before Normal)
