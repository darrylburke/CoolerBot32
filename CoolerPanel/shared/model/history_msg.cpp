#include "history_msg.h"
#include <ArduinoJson.h>
#include <cmath>

static constexpr int64_t kStep = 60;
static constexpr size_t kMaxSlots = 1440;
static constexpr int64_t kMaxAhead = 120;
static constexpr int64_t kMinValidEpoch = 1600000000;   // 2020: before this a clock is unset

static int16_t c10(double v) {
    double scaled = v * 10.0;
    if (scaled > 32767.0) scaled = 32767.0;
    if (scaled < -32768.0) scaled = -32768.0;
    return (int16_t)std::lround(scaled);
}

bool history_msg_parse(const char* json, size_t len, int64_t now, HistoryMsg& out) {
    JsonDocument doc;
    if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;
    JsonObjectConst o = doc.as<JsonObjectConst>();
    if (o.isNull()) return false;
    if (!o["v"].is<int>() || o["v"].as<int>() != 1) return false;
    if (!o["interval_s"].is<int>() || o["interval_s"].as<int>() != kStep) return false;
    if (!o["t0"].is<int64_t>()) return false;
    const int64_t t0 = o["t0"].as<int64_t>();
    if (t0 < kMinValidEpoch) return false;
    JsonArrayConst temp = o["temp"].as<JsonArrayConst>();
    JsonArrayConst hum = o["hum"].as<JsonArrayConst>();
    JsonArrayConst relay = o["relay"].as<JsonArrayConst>();
    if (temp.isNull() || hum.isNull() || relay.isNull()) return false;
    const size_t n = temp.size();
    if (n == 0 || n > kMaxSlots || hum.size() != n || relay.size() != n) return false;
    const int64_t to = t0 + (int64_t)n * kStep;
    if (now >= kMinValidEpoch && to > now + kMaxAhead) return false;

    out.from = t0;
    out.to = to;
    out.samples.clear();
    out.samples.reserve(n);
    for (size_t i = 0; i < n; i++) {
        JsonVariantConst t = temp[i], h = hum[i], r = relay[i];
        if (!t.is<double>() || !h.is<double>()) continue;
        Sample s{t0 + (int64_t)i * kStep, c10(t.as<double>()), c10(h.as<double>()),
                 (uint8_t)(r.is<int>() && r.as<int>() == 1 ? 1 : 0)};
        out.samples.push_back(s);
    }
    return true;
}

void history_missing(const History& h, const HistoryMsg& m, std::vector<Sample>& out) {
    out.clear();
    // Held samples with a real clock, oldest first; i walks them alongside m.
    size_t first = 0;
    while (first < h.size() && h.at(first).t < kMinValidEpoch) first++;
    const bool any = first < h.size();
    const int64_t newest = any ? h.newest_epoch() : 0;
    size_t i = first;
    for (const Sample& s : m.samples) {
        if (any && s.t + kStep > newest) break;          // live data's minutes
        while (i < h.size() && h.at(i).t < s.t - kStep) i++;
        const bool held = i < h.size() && h.at(i).t < s.t + 2 * kStep;
        if (!held) out.push_back(s);
    }
}
