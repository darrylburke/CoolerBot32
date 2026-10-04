#pragma once
#include <string>
#include "alarm.h"

// Settings that belong to the panel, not the cooler. Never published to
// cooler/cmd -- the controller neither knows nor cares about these.
struct PanelConfig {
    AlarmCfg alarm;              // thresholds + hold-off (spec section 7)
    int  backlight = 80;         // percent, 5..100
    bool night_dim = true;
    int  default_zoom_s = 10800; // 3600 | 10800 | 21600

    // Humidity band for the Trend card's colour. A walk-in wants HIGH humidity
    // (dry air desiccates produce) but not so high it condenses on the coil,
    // so this is a band with a warning either side, not a simple maximum.
    //   below hum_low  -> amber (too dry)
    //   within         -> green
    //   above hum_high -> red   (condensation / mould risk)
    int  hum_low  = 50;          // %RH, 0..100, must stay below hum_high
    int  hum_high = 80;          // %RH, 0..100
};

std::string panel_config_to_json(const PanelConfig& c);
// Overlays parsed fields onto `out`; absent keys keep out's current value,
// and every numeric field is clamped to its valid range on the way in --
// this is the only path flash/disk-sourced values take before driving the
// alarm system, so a corrupted or hand-edited file must not be able to
// e.g. set holdoff_s absurdly high (silently suppressing alarms) or
// backlight to 0 (panel appears dead). Returns false only on a JSON parse
// error.
bool panel_config_from_json(const char* json, PanelConfig& out);
