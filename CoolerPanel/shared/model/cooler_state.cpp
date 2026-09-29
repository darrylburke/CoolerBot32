#include "cooler_state.h"
#include <ArduinoJson.h>
#include <cstring>

static void copy_str(char* dst, size_t cap, const char* src) {
    if (!src) { dst[0] = 0; return; }
    std::strncpy(dst, src, cap - 1);
    dst[cap - 1] = 0;
}

// Absent key: keep the prior value (and its validity). Explicit null: the
// controller is saying "no reading" -- mark invalid, keep the last number so
// nothing downstream ever sees a fabricated 0. isUnbound() is what tells the
// two apart (ArduinoJson 7: a missing key is unbound, a JSON null is bound).
static void read_opt(JsonObjectConst o, const char* k, float& out, bool& valid) {
    JsonVariantConst v = o[k];
    if (v.isUnbound()) return;
    if (v.is<float>() || v.is<int>()) { out = v.as<float>(); valid = true; }
    else if (v.isNull()) valid = false;
}

template <typename T>
static void read_num(JsonObjectConst o, const char* k, T& out) {
    JsonVariantConst v = o[k];
    if (v.is<T>()) out = v.as<T>();
}

static void read_str(JsonObjectConst o, const char* k, char* dst, size_t cap) {
    JsonVariantConst v = o[k];
    if (v.is<const char*>()) copy_str(dst, cap, v.as<const char*>());
}

bool cooler_parse_data(CoolerState& s, const char* json, size_t len, int64_t now_epoch) {
    JsonDocument doc;
    if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;
    if (!doc.is<JsonObjectConst>()) return false;
    JsonObjectConst o = doc.as<JsonObjectConst>();
    // Schema gate: only the v4 controller's payload. A retained v3 /data
    // (two-unit fields, no "v") must be ignored, not half-parsed.
    if (!o["v"].is<int>() || o["v"].as<int>() != 2) return false;

    read_num(o, "coolerset", s.coolerset);
    read_num(o, "range", s.range);
    read_num(o, "fin_cutoff", s.fin_cutoff);
    read_num(o, "fin_recover", s.fin_recover);
    read_num(o, "settle", s.settle);
    read_num(o, "minofftime", s.minofftime);
    read_num(o, "minruntime", s.minruntime);
    read_num(o, "maxrun", s.maxrun);
    read_num(o, "dutypercent", s.dutypercent);
    read_num(o, "sampleinterval", s.sampleinterval);

    // Snapshot the calibration fields so the end of a run can be classified.
    const int prev_active = s.cal_active, prev_cal = s.fin_cal;
    const float prev_beta = s.fin_beta, prev_r0 = s.fin_r0, prev_err = s.fin_cal_err;
    const bool prev_err_valid = s.fin_cal_err_valid;

    read_opt(o, "temp", s.temp, s.temp_valid);
    read_opt(o, "humidity", s.humidity, s.humidity_valid);
    read_opt(o, "fin_temp", s.fin_temp, s.fin_temp_valid);
    bool unused;
    read_opt(o, "fin_ohms", s.fin_ohms, unused);
    read_opt(o, "fin_slope", s.fin_slope, s.fin_slope_valid);

    read_str(o, "mode", s.mode, sizeof(s.mode));
    read_str(o, "override_src", s.override_src, sizeof(s.override_src));
    read_str(o, "state", s.state, sizeof(s.state));
    read_num(o, "relay", s.relay);
    read_num(o, "cool_call", s.cool_call);
    read_num(o, "defrost", s.defrost);
    {
        JsonVariantConst c = o["compressor"];
        if (c.is<int>()) s.compressor = c.as<int>();
        else if (!c.isUnbound() && c.isNull()) s.compressor = -1;
    }
    read_num(o, "sht_fault", s.sht_fault);
    read_num(o, "fin_fault", s.fin_fault);
    read_num(o, "no_response", s.no_response);
    read_num(o, "run_s", s.run_s);
    read_num(o, "off_s", s.off_s);
    read_num(o, "hold_s", s.hold_s);

    read_num(o, "fin_cal", s.fin_cal);
    read_num(o, "fin_beta", s.fin_beta);
    read_num(o, "fin_r0", s.fin_r0);
    read_opt(o, "fin_cal_err", s.fin_cal_err, s.fin_cal_err_valid);
    read_num(o, "cal_active", s.cal_active);
    read_num(o, "cal_points", s.cal_points);
    read_num(o, "cal_span", s.cal_span);

    read_num(o, "uptime_s", s.uptime_s);

    if (s.cal_active) {
        s.cal_result = CAL_RESULT_NONE;
    } else if (prev_active) {
        // Accepted fits change beta/r0 (or first set fin_cal); a rejected fit
        // only publishes its error; an abort or timeout changes neither.
        if (s.fin_cal && (!prev_cal || s.fin_beta != prev_beta || s.fin_r0 != prev_r0))
            s.cal_result = CAL_RESULT_CALIBRATED;
        else if (s.fin_cal_err_valid && (!prev_err_valid || s.fin_cal_err != prev_err))
            s.cal_result = CAL_RESULT_REJECTED;
        else
            s.cal_result = CAL_RESULT_NO_FIT;
    }

    s.last_rx_epoch = now_epoch;
    s.valid = true;
    return true;
}

bool cooler_in_override(const CoolerState& s) {
    return std::strncmp(s.mode, "override", 8) == 0;
}
