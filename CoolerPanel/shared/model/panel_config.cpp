#include "panel_config.h"
#include <ArduinoJson.h>

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

std::string panel_config_to_json(const PanelConfig& c) {
    JsonDocument d;
    auto a = d["alarm"].to<JsonObject>();
    a["silent_s"]  = c.alarm.silent_s;
    a["over_c"]    = c.alarm.over_c;
    a["over_s"]    = c.alarm.over_s;
    a["holdoff_s"] = c.alarm.holdoff_s;
    d["backlight"]      = c.backlight;
    d["night_dim"]      = c.night_dim;
    d["default_zoom_s"] = c.default_zoom_s;
    d["hum_low"]        = c.hum_low;
    d["hum_high"]       = c.hum_high;
    std::string out;
    serializeJson(d, out);
    return out;
}

bool panel_config_from_json(const char* json, PanelConfig& out) {
    JsonDocument d;
    if (deserializeJson(d, json) != DeserializationError::Ok) return false;
    PanelConfig c;   // start from defaults so absent keys keep them
    if (d["alarm"].is<JsonObject>()) {
        auto a = d["alarm"];
        c.alarm.silent_s  = a["silent_s"]  | c.alarm.silent_s;
        c.alarm.over_c    = a["over_c"]    | c.alarm.over_c;
        c.alarm.over_s    = a["over_s"]    | c.alarm.over_s;
        c.alarm.holdoff_s = a["holdoff_s"] | c.alarm.holdoff_s;
    }
    c.backlight      = d["backlight"]      | c.backlight;
    c.night_dim      = d["night_dim"]      | c.night_dim;
    c.default_zoom_s = d["default_zoom_s"] | c.default_zoom_s;
    c.hum_low        = d["hum_low"]        | c.hum_low;
    c.hum_high       = d["hum_high"]       | c.hum_high;

    // Clamp on LOAD, not just on save: these values come off flash/disk and
    // drive the alarm system directly, so a corrupted or hand-edited file
    // must not be able to silently suppress alarms or blank the backlight.
    c.alarm.silent_s  = clampi(c.alarm.silent_s,  30, 3600);
    c.alarm.over_c    = clampi(c.alarm.over_c,     1,   20);
    c.alarm.over_s    = clampi(c.alarm.over_s,    60, 86400);
    c.alarm.holdoff_s = clampi(c.alarm.holdoff_s,  0, 86400);
    c.backlight       = clampi(c.backlight,        5,  100);
    if (c.default_zoom_s != 3600 && c.default_zoom_s != 10800 &&
        c.default_zoom_s != 21600) c.default_zoom_s = 3600;
    c.hum_low  = clampi(c.hum_low,  0, 100);
    c.hum_high = clampi(c.hum_high, 0, 100);
    // An inverted band would paint every reading red AND amber depending on
    // evaluation order -- meaningless. Force a sane ordering instead.
    if (c.hum_low >= c.hum_high) { c.hum_low = 50; c.hum_high = 80; }

    out = c;
    return true;
}
