#pragma once
#include <stdint.h>
#include <stddef.h>

// Mirrors the v4 single-relay controller's /data payload, schema "v": 2.
// Field meanings: docs/superpowers/specs/2026-09-26-single-relay-controller-design.md §3.1
// (in the parent Cooler directory).
// How the last calibration run ended, derived by the panel from the cal_active
// 1 -> 0 transition (the controller publishes no verdict of its own).
enum { CAL_RESULT_NONE = 0, CAL_RESULT_CALIBRATED = 1, CAL_RESULT_REJECTED = 2, CAL_RESULT_NO_FIT = 3 };

struct CoolerState {
    // ---- settable config (spec §2.11) ----
    int coolerset = 0;
    int range = 0;
    int fin_cutoff = 0;      // C
    int fin_recover = 0;     // C
    int settle = 0;          // minutes
    int minofftime = 0;      // minutes
    int minruntime = 0;      // seconds
    int maxrun = 0;          // minutes
    int dutypercent = 0;
    int sampleinterval = 0;  // seconds

    // ---- live readings. *_valid is false while the controller publishes null
    //      (sensor failed); the value keeps its last good reading.
    float temp = 0.0f;       bool temp_valid = false;
    float humidity = 0.0f;   bool humidity_valid = false;
    float fin_temp = 0.0f;   bool fin_temp_valid = false;
    float fin_ohms = 0.0f;
    float fin_slope = 0.0f;  bool fin_slope_valid = false;  // C/min; null for the first 60 s

    // ---- run state ----
    char mode[16] = {0};          // normal | override | fin-proxy | override-proxy | blind
    char override_src[8] = {0};   // none | switch | link | both
    char state[8] = {0};          // cooling | idle | defrost | wait | rest
    int relay = 0;
    int cool_call = 0;
    int defrost = 0;
    int compressor = -1;          // -1 unknown (null), 0 stopped, 1 running
    int sht_fault = 0;
    int fin_fault = 0;
    int no_response = 0;
    uint32_t run_s = 0, off_s = 0, hold_s = 0;

    // ---- fin calibration ----
    int fin_cal = 0;
    float fin_beta = 0.0f, fin_r0 = 0.0f;
    float fin_cal_err = 0.0f;  bool fin_cal_err_valid = false;
    int cal_active = 0, cal_points = 0;
    float cal_span = 0.0f;
    int cal_result = CAL_RESULT_NONE;   // panel-derived, see the enum above

    uint32_t uptime_s = 0;

    // ---- panel-side meta ----
    int64_t last_rx_epoch = 0;
    bool valid = false;
    bool online = false;     // from the availability topic
    // True once ANY /availability message (online or offline) has ever been
    // routed, independent of `valid` (which only tracks /data). A panel that
    // has never heard from the broker at all must stay silent; one that has
    // received a retained "offline" LWT but no /data yet must still alarm.
    bool availability_seen = false;
};

// Parse a /data payload. Rejects (returns false, leaves s untouched) anything
// that is not a JSON object carrying "v": 2 -- including a stale retained v3
// payload, which has no "v". now_epoch stamps last_rx_epoch.
bool cooler_parse_data(CoolerState& s, const char* json, size_t len, int64_t now_epoch);

// True in "override" and "override-proxy": the controller's fixed 5 / 3 C
// thermostat is in force and coolerset/range are not.
bool cooler_in_override(const CoolerState& s);
