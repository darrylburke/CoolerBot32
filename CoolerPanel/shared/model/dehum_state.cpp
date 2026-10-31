#include "dehum_state.h"
#include <ArduinoJson.h>
#include <cstdio>
#include <cstring>

const DehumMode DEHUM_MODE_BUTTONS[DEHUM_MODE_BUTTONS_N] = {
    DehumMode::On, DehumMode::Off, DehumMode::Timer, DehumMode::Auto, DehumMode::Cycle,
};

const char* dehum_mode_wire(DehumMode m) {
    switch (m) {
    case DehumMode::Auto:  return "auto";
    case DehumMode::On:    return "on";
    case DehumMode::Off:   return "off";
    case DehumMode::Timer: return "timer";
    case DehumMode::Cycle: return "cycle";
    default:               return "";
    }
}

const char* dehum_mode_label(DehumMode m) {
    switch (m) {
    case DehumMode::Auto:  return "AUTO (rules)";
    case DehumMode::On:    return "ON";
    case DehumMode::Off:   return "OFF";
    case DehumMode::Timer: return "TIMER";
    case DehumMode::Cycle: return "CYCLE";
    default:               return "--";
    }
}

DehumMode dehum_mode_of(const char* wire) {
    if (!wire) return DehumMode::Unknown;
    for (DehumMode m : {DehumMode::Auto, DehumMode::On, DehumMode::Off, DehumMode::Timer, DehumMode::Cycle})
        if (!std::strcmp(dehum_mode_wire(m), wire)) return m;
    return DehumMode::Unknown;
}

// A number, not a string or null (the app's JsonObject.num()).
static bool is_num(JsonVariantConst v) { return v.is<int64_t>() || v.is<double>(); }

static bool flag(JsonObjectConst o, const char* k) {
    JsonVariantConst v = o[k];
    return v.is<int>() && v.as<int>() == 1;
}

static uint32_t secs(JsonObjectConst o, const char* k) {
    JsonVariantConst v = o[k];
    if (!v.is<int64_t>()) return 0;
    const int64_t x = v.as<int64_t>();
    return x < 0 ? 0 : (uint32_t)x;
}

static void reading(JsonObjectConst o, const char* k, float& out, bool& valid) {
    JsonVariantConst v = o[k];
    valid = is_num(v);
    if (valid) out = v.as<float>();
}

bool dehum_parse_data(DehumState& s, const char* json, size_t len, int64_t now_epoch) {
    JsonDocument doc;
    if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;
    if (!doc.is<JsonObjectConst>()) return false;
    JsonObjectConst o = doc.as<JsonObjectConst>();
    if (!o["v"].is<int>() || o["v"].as<int>() != 1) return false;

    DehumState d;
    d.online = s.online;
    d.availability_seen = s.availability_seen;

    JsonVariantConst m = o["mode"];
    d.mode = m.is<const char*>() ? dehum_mode_of(m.as<const char*>()) : DehumMode::Unknown;
    d.relay = flag(o, "relay");
    d.relay2 = flag(o, "relay2");
    d.link_lost = flag(o, "link_lost");
    d.hold_s = secs(o, "hold_s");
    d.hold2_s = secs(o, "hold2_s");
    d.remain_s = secs(o, "remain_s");
    {
        JsonVariantConst p = o["phase"];
        const char* ph = p.is<const char*>() ? p.as<const char*>() : nullptr;
        d.phase = !ph ? -1 : !std::strcmp(ph, "on") ? 1 : !std::strcmp(ph, "off") ? 0 : -1;
    }
    reading(o, "temp", d.temp, d.temp_valid);
    reading(o, "humidity", d.humidity, d.humidity_valid);
    for (int n = 1; n <= DEHUM_RULES; n++) {
        char k[12];
        std::snprintf(k, sizeof k, "r%d_match", n);
        d.rule_match[n - 1] = flag(o, k);
    }
    // Settings are integers on the wire; anything else leaves the key unknown.
    for (int i = 0; i < DEHUM_NKEYS; i++) {
        JsonVariantConst v = o[dehum_key(i)];
        if (v.is<int>()) { d.settings.v[i] = v.as<int>(); d.settings.known[i] = true; }
    }
    d.run_s = secs(o, "run_s");
    d.uptime_s = secs(o, "uptime_s");

    d.last_rx_epoch = now_epoch;
    d.valid = true;
    s = d;
    return true;
}

bool dehum_parse_availability(DehumState& s, const char* payload, size_t len) {
    if (!payload) return false;
    // Trim like the app: a stray newline from a hand-typed publish still counts.
    while (len && (payload[len - 1] == '\n' || payload[len - 1] == '\r' || payload[len - 1] == ' ')) len--;
    while (len && (*payload == ' ' || *payload == '\n')) { payload++; len--; }
    if (len == 6 && !std::strncmp(payload, "online", 6))       s.online = true;
    else if (len == 7 && !std::strncmp(payload, "offline", 7)) s.online = false;
    else return false;
    s.availability_seen = true;
    return true;
}
