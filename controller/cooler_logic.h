// cooler_logic.h -- every control decision for the v4 single-relay cooler.
//
// Pure C++17: no ESPHome, Arduino or libc beyond <cmath>/<cstdint>. The
// ESPHome YAML fills Inputs from the hardware once a second, calls step(),
// and drives the relay / LED from the returned Outputs. Host tests compile
// this same header (controller/tests).
//
// Spec: docs/superpowers/specs/2026-09-26-single-relay-controller-design.md
#pragma once
#include <cmath>
#include <cstdint>

#ifndef COOLER_OVR_ON_C
#define COOLER_OVR_ON_C 5.0f
#endif
#ifndef COOLER_OVR_OFF_C
#define COOLER_OVR_OFF_C 3.0f
#endif

namespace cooler {

// ---- fixed constants (spec §1.4, §2.1, §2.2, §2.8, §2.9) -------------------
constexpr float OVR_ON_C = COOLER_OVR_ON_C;   // override thermostat: on at >=
constexpr float OVR_OFF_C = COOLER_OVR_OFF_C; // override thermostat: off at <=

constexpr float FIN_R_FIXED = 32000.0f;  // divider resistor, 3.3 V -> R -> ADC (22k + 10k as built)
constexpr float FIN_VREF = 3.3f;
constexpr float FIN_V_MIN = 0.05f, FIN_V_MAX = 3.2f;
constexpr float FIN_OHMS_MIN = 200.0f, FIN_OHMS_MAX = 1.0e6f;
// Plausibility: an evaporator never reads outside this. The ESP32 ADC saturates
// near 3.1 V (12 dB), below the 1 Mohm open threshold, so an open probe reads
// about -40 C -- which would otherwise latch defrost forever with no fault.
constexpr float FIN_C_MIN = -30.0f, FIN_C_MAX = 60.0f;
constexpr float T0_K = 298.15f;          // Beta reference temperature (25 C)

constexpr uint32_t SHT_FAULT_MS = 300000;   // no valid SHT30 reading for 300 s
constexpr uint32_t BOX_STALE_MS = 30000;    // a reading older than this is not a reading
constexpr uint32_t FIN_DEBOUNCE_MS = 10000; // fin fault set / clear debounce
constexpr uint32_t LINK_DEBOUNCE_MS = 60000;// link_lost set / clear debounce

constexpr uint32_t FIN_SAMPLE_MS = 5000;    // fin ring cadence
constexpr int SLOPE_N = 12;                 // 12 x 5 s = 60 s slope window
constexpr int SLOPE_MIN_N = SLOPE_N;        // spec §2.8: a full 60 s window -- shorter
                                            // windows let ADC noise fake a -0.5 C/min slope
constexpr float COMP_ON_SLOPE = -0.5f;      // C/min: latch compressor = 1
constexpr float COMP_OFF_SLOPE = 0.3f;      // C/min: latch compressor = 0
constexpr float NEAR_BOX_C = 1.0f;          // |fin - box| "coil at air temp"
constexpr uint32_t NEAR_BOX_DEFROST_MS = 120000; // defrost exit via fin ~ box
constexpr uint32_t NEAR_BOX_COMP_MS = 60000;     // relay open this long first
constexpr uint32_t NO_RESPONSE_MS = 600000;      // 10 min closed, no compressor

constexpr int CAL_MAX_POINTS = 12;
constexpr int CAL_STAB_N = 24;              // 24 x 5 s = 120 s stability window
constexpr float CAL_STAB_TEMP_C = 0.2f;
constexpr float CAL_STAB_OHMS_FRAC = 0.01f;
constexpr float CAL_MIN_SEP_C = 1.0f;
constexpr int CAL_MIN_POINTS = 4;
constexpr float CAL_MIN_SPAN_C = 8.0f;
constexpr float CAL_BETA_MIN = 2500.0f, CAL_BETA_MAX = 5500.0f;
constexpr float CAL_MAX_ERR_C = 1.0f;
constexpr uint32_t CAL_TIMEOUT_MS = 48u * 3600u * 1000u;  // give up; keep previous values

// ---- settings (spec §2.11), persisted by the YAML ---------------------------
struct Settings {
    int coolerset = 4;       // C       2 .. 40
    int range = 2;           // C       0 .. 5
    int fin_cutoff = 1;      // C      -5 .. 5  (bench: 0 never tripped against ice)
    int fin_recover = 3;     // C       fin_cutoff+1 .. 10
    int settle = 10;         // min     2 .. 30
    int minofftime = 5;      // min     0 .. 30
    int minruntime = 180;    // s       0 .. 600
    int maxrun = 10;         // min     1 .. 60
    int dutypercent = 50;    // %       1 .. 100
    int sampleinterval = 3600; // s    10 .. 3600
};

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Clamp every field to its bounds. fin_cutoff is clamped first; fin_recover's
// floor then tracks it, so raising fin_cutoff bumps fin_recover up.
inline void clamp_settings(Settings& s) {
    s.coolerset = clampi(s.coolerset, 2, 40);
    s.range = clampi(s.range, 0, 5);
    s.fin_cutoff = clampi(s.fin_cutoff, -5, 5);
    s.fin_recover = clampi(s.fin_recover, s.fin_cutoff + 1, 10);
    s.settle = clampi(s.settle, 2, 30);
    s.minofftime = clampi(s.minofftime, 0, 30);
    s.minruntime = clampi(s.minruntime, 0, 600);
    s.maxrun = clampi(s.maxrun, 1, 60);
    s.dutypercent = clampi(s.dutypercent, 1, 100);
    s.sampleinterval = clampi(s.sampleinterval, 10, 3600);
}

// ---- fin thermistor calibration (spec §2.7), persisted by the YAML ---------
struct FinCal {
    float beta = 3950.0f;
    float r0 = 10000.0f;     // ohms at 25 C
    bool calibrated = false;
    float last_err = NAN;    // RMS residual of the last fit attempt, C
};

inline void fincal_reset(FinCal& c) { c = FinCal{}; }

// Divider: 3.3 V -> R_FIXED -> ADC -> thermistor -> GND.
inline float fin_ohms_from_volts(float v) {
    if (!(v > 0.0f) || !(v < FIN_VREF)) return NAN;
    return FIN_R_FIXED * v / (FIN_VREF - v);
}

inline float fin_c_from_ohms(float ohms, const FinCal& c) {
    if (!(ohms > 0.0f)) return NAN;
    float inv_t = 1.0f / T0_K + std::log(ohms / c.r0) / c.beta;
    return 1.0f / inv_t - 273.15f;
}

struct CalPoint { float ohms; float temp_c; };
struct CalFit { bool ok; float beta; float r0; float err_c; };

// Least-squares fit of 1/T = a + b*ln(R). B = 1/b, R0 from a at T0.
inline CalFit fit_beta(const CalPoint* p, int n) {
    CalFit f{false, NAN, NAN, NAN};
    if (n < 2) return f;
    double sx = 0, sy = 0;
    for (int i = 0; i < n; i++) { sx += std::log((double)p[i].ohms); sy += 1.0 / (p[i].temp_c + 273.15); }
    double mx = sx / n, my = sy / n, sxx = 0, sxy = 0;
    for (int i = 0; i < n; i++) {
        double dx = std::log((double)p[i].ohms) - mx, dy = 1.0 / (p[i].temp_c + 273.15) - my;
        sxx += dx * dx; sxy += dx * dy;
    }
    if (sxx <= 0) return f;
    double b = sxy / sxx, a = my - b * mx;
    if (!(b > 0)) return f;
    double beta = 1.0 / b;
    double r0 = std::exp((1.0 / T0_K - a) * beta);
    double se = 0;
    for (int i = 0; i < n; i++) {
        double t = 1.0 / (a + b * std::log((double)p[i].ohms)) - 273.15;
        se += (t - p[i].temp_c) * (t - p[i].temp_c);
    }
    f.ok = true; f.beta = (float)beta; f.r0 = (float)r0; f.err_c = (float)std::sqrt(se / n);
    return f;
}

// ---- per-tick inputs / outputs ---------------------------------------------
struct Inputs {
    bool box_valid = false;   // SHT30 reading present this tick
    float box_c = NAN;
    uint32_t box_age_ms = 0;  // ms since the SHT30 last delivered a reading. ESPHome's
                              // sht3xd keeps the last value when the sensor dies, so
                              // age -- not NaN -- is what reveals a dead sensor.
    float fin_volts = NAN;    // latest ADC reading, NAN if none
    bool switch_closed = false; // latching override switch, debounced
    bool mqtt_connected = false;
};

enum class Mode : uint8_t { Normal, Override, FinProxy, OverrideProxy, Blind };
enum class OverrideSrc : uint8_t { None, Switch, Link, Both };
enum class RunState : uint8_t { Idle, Cooling, Defrost, Wait, Rest };
enum class Led : uint8_t { Off, Solid, Slow, Fast };

inline const char* mode_str(Mode m) {
    switch (m) {
        case Mode::Normal: return "normal";
        case Mode::Override: return "override";
        case Mode::FinProxy: return "fin-proxy";
        case Mode::OverrideProxy: return "override-proxy";
        default: return "blind";
    }
}
inline const char* ovr_str(OverrideSrc o) {
    switch (o) {
        case OverrideSrc::Switch: return "switch";
        case OverrideSrc::Link: return "link";
        case OverrideSrc::Both: return "both";
        default: return "none";
    }
}
inline const char* state_str(RunState s) {
    switch (s) {
        case RunState::Cooling: return "cooling";
        case RunState::Defrost: return "defrost";
        case RunState::Wait: return "wait";
        case RunState::Rest: return "rest";
        default: return "idle";
    }
}

struct Outputs {
    bool relay = false;
    Led led = Led::Off;
    Mode mode = Mode::Normal;
    OverrideSrc override_src = OverrideSrc::None;
    RunState state = RunState::Idle;
    bool cool_call = false;
    bool defrost = false;
    bool sht_fault = false;
    bool fin_fault = false;
    bool link_lost = false;
    bool no_response = false;
    int compressor = -1;      // -1 = null (fin fault), 0, 1
    float fin_c = NAN;        // NAN = null
    float fin_ohms = NAN;
    float fin_slope = NAN;    // C/min
    uint32_t run_s = 0, off_s = 0, hold_s = 0;
    bool cal_active = false;
    int cal_points = 0;
    float cal_span = 0.0f;
    bool box_valid = false;   // fresh SHT30 reading this tick (temp/humidity publishable)
    bool publish_now = false; // a tracked field changed this tick
};

// ---- persistent-across-ticks state -----------------------------------------
struct LogicState {
    bool init = false;

    bool relay = false;
    // Time in the current relay state, accumulated tick by tick and saturated,
    // so a relay left open for more than 49.7 days never aliases back to a
    // few seconds (which now - changed_ms would).
    uint32_t relay_age_ms = 0;       // 0 at boot: "opened at boot"
    uint32_t last_now = 0;

    uint32_t sht_last_good_ms = 0;
    bool sht_fault = false;

    bool fin_fault = false;
    bool fin_last_valid = true;
    uint32_t fin_valid_changed_ms = 0;

    bool link_lost = false;
    bool link_last = false;
    uint32_t link_changed_ms = 0;

    bool demand = false;             // thermostat hold (normal + override share it)

    Mode last_mode = Mode::Normal;
    bool proxy_cool = false;         // fin-proxy phase: false REST, true COOL

    bool defrost = false;
    bool near_box = false;
    uint32_t near_box_since = 0;

    bool duty_active = false;
    uint32_t duty_start_ms = 0;

    float slope_ring[SLOPE_N] = {};
    uint32_t slope_t[SLOPE_N] = {};  // when each sample was taken
    int slope_n = 0, slope_head = 0;
    uint32_t last_sample_ms = 0;
    bool sampled_once = false;

    int compressor = 0;              // -1 null, 0, 1
    bool nr_timing = false;
    uint32_t nr_since = 0;
    bool no_response = false;

    bool cal_active = false;
    bool cal_started = false;        // cal_started_ms is set on the first tick of a run
    uint32_t cal_started_ms = 0;
    CalPoint cal_pts[CAL_MAX_POINTS] = {};
    int cal_n = 0;
    float stab_box[CAL_STAB_N] = {}, stab_ohms[CAL_STAB_N] = {};
    int stab_n = 0, stab_head = 0;

    // change detection for publish_now
    uint32_t sig = 0;
};

// A start while a run is active is ignored: it must not throw away points.
inline void cal_start(LogicState& st) { if (st.cal_active) return; st.cal_active = true; st.cal_started = false; st.cal_n = 0; st.stab_n = 0; st.stab_head = 0; }
inline void cal_abort(LogicState& st) { st.cal_active = false; st.cal_n = 0; }

// Least-squares slope of the ring, in C per minute, over the samples' real
// timestamps -- ticks can run late, so "5 s apart" is only nominal.
inline float ring_slope(const LogicState& st) {
    int n = st.slope_n;
    if (n < SLOPE_MIN_N) return NAN;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < n; i++) {
        int idx = (st.slope_head - n + i + SLOPE_N) % SLOPE_N;   // oldest first
        const int first = (st.slope_head - n + SLOPE_N) % SLOPE_N;
        double x = (st.slope_t[idx] - st.slope_t[first]) / 60000.0, y = st.slope_ring[idx];
        sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    double d = n * sxx - sx * sx;
    return d == 0 ? NAN : (float)((n * sxy - sx * sy) / d);
}

inline uint32_t sec_left(uint32_t elapsed_ms, uint32_t limit_ms) {
    return elapsed_ms >= limit_ms ? 0 : (limit_ms - elapsed_ms + 999) / 1000;
}

// One control tick. Call once a second (and again after any /cmd).
inline Outputs step(const Inputs& in, const Settings& s, FinCal& cal, LogicState& st, uint32_t now) {
    Outputs o;
    if (!st.init) {
        st.init = true;
        st.relay = false;
        st.relay_age_ms = 0;             // minofftime counts from boot
        st.last_now = now;
        st.sht_last_good_ms = now;
        st.fin_valid_changed_ms = now;
        st.link_last = in.mqtt_connected;
        st.link_changed_ms = now;
        st.last_sample_ms = now;
        st.last_mode = Mode::Normal;
    }
    {
        const uint32_t dt = now - st.last_now;
        st.last_now = now;
        // relay_age_ms never exceeds the cap, so the subtraction cannot underflow.
        st.relay_age_ms = (dt >= 0x7FFFFFFFu - st.relay_age_ms) ? 0x7FFFFFFFu : st.relay_age_ms + dt;
    }
    const uint32_t since_relay = st.relay_age_ms;

    // ---- faults (§2.1) ----
    const bool box_fresh = in.box_valid && in.box_age_ms < BOX_STALE_MS;
    if (box_fresh) { st.sht_last_good_ms = now; st.sht_fault = false; }
    else if (now - st.sht_last_good_ms > SHT_FAULT_MS) { st.sht_fault = true; }

    float ohms = fin_ohms_from_volts(in.fin_volts);
    bool fin_valid = in.fin_volts >= FIN_V_MIN && in.fin_volts <= FIN_V_MAX &&
                     ohms >= FIN_OHMS_MIN && ohms <= FIN_OHMS_MAX;
    if (fin_valid) {
        const float c = fin_c_from_ohms(ohms, cal);
        fin_valid = c >= FIN_C_MIN && c <= FIN_C_MAX;
    }
    if (fin_valid != st.fin_last_valid) { st.fin_last_valid = fin_valid; st.fin_valid_changed_ms = now; }
    if (now - st.fin_valid_changed_ms >= FIN_DEBOUNCE_MS) st.fin_fault = !fin_valid;
    const bool fin_ok = fin_valid && !st.fin_fault;   // usable this tick
    const float fin = fin_ok ? fin_c_from_ohms(ohms, cal) : NAN;
    const bool box_ok = box_fresh && !st.sht_fault;

    if (in.mqtt_connected != st.link_last) { st.link_last = in.mqtt_connected; st.link_changed_ms = now; }
    if (now - st.link_changed_ms >= LINK_DEBOUNCE_MS) st.link_lost = !in.mqtt_connected;

    // ---- 5 s fin sampling: slope ring + calibration stability ring ----
    bool sampled = false;
    if (!st.sampled_once || now - st.last_sample_ms >= FIN_SAMPLE_MS) {
        st.sampled_once = true;
        st.last_sample_ms = now;
        sampled = true;
        if (fin_ok) {
            st.slope_ring[st.slope_head] = fin;
            st.slope_t[st.slope_head] = now;
            st.slope_head = (st.slope_head + 1) % SLOPE_N;
            if (st.slope_n < SLOPE_N) st.slope_n++;
        } else if (st.fin_fault) {
            st.slope_n = 0;
        }   // invalid but not yet a fault: skip this sample, keep the window
    }
    const float slope = fin_ok ? ring_slope(st) : NAN;

    // ---- compressor inference (§2.8) ----
    if (st.fin_fault) {
        st.compressor = -1;
    } else if (fin_ok) {
        if (st.compressor < 0) st.compressor = 0;
        if (!std::isnan(slope) && slope <= COMP_ON_SLOPE) st.compressor = 1;
        else if (!std::isnan(slope) && slope >= COMP_OFF_SLOPE) st.compressor = 0;
        else if (!st.relay && since_relay >= NEAR_BOX_COMP_MS && box_ok &&
                 std::fabs(fin - in.box_c) <= NEAR_BOX_C) st.compressor = 0;
    }

    // ---- mode (§2.2) ----
    const bool sw = in.switch_closed, ln = st.link_lost;
    const bool ovr = sw || ln;
    Mode mode;
    if (st.sht_fault && st.fin_fault) mode = Mode::Blind;
    else if (ovr) mode = st.sht_fault ? Mode::OverrideProxy : Mode::Override;
    else mode = st.sht_fault ? Mode::FinProxy : Mode::Normal;
    const bool proxy = mode == Mode::FinProxy || mode == Mode::OverrideProxy;
    const bool was_proxy = st.last_mode == Mode::FinProxy || st.last_mode == Mode::OverrideProxy;
    if (proxy && !was_proxy) st.proxy_cool = false;
    st.last_mode = mode;

    const float on_c = (mode == Mode::Override || mode == Mode::OverrideProxy)
                           ? OVR_ON_C : (float)(s.coolerset + s.range);
    const float off_c = (mode == Mode::Override || mode == Mode::OverrideProxy)
                            ? OVR_OFF_C : (float)(s.coolerset - s.range);

    // ---- demand (§2.2, §2.3) ----
    bool demand = false, settle_pending = false;
    const uint32_t settle_ms = (uint32_t)s.settle * 60000u;
    if (mode == Mode::Normal || mode == Mode::Override) {
        if (box_fresh) {
            if (mode == Mode::Normal) {
                if (in.box_c > on_c) st.demand = true;
                if (in.box_c < off_c) st.demand = false;
            } else {
                if (in.box_c >= on_c) st.demand = true;
                if (in.box_c <= off_c) st.demand = false;
            }
        }
        demand = st.demand;
    } else if (proxy) {
        if (st.proxy_cool) {
            bool ran_out = st.relay && since_relay >= (uint32_t)s.maxrun * 60000u;
            if ((fin_ok && fin <= (float)s.fin_cutoff) || ran_out) st.proxy_cool = false;
        } else if (!st.relay) {
            if (since_relay < settle_ms) settle_pending = true;
            else if (fin_ok && fin >= on_c) st.proxy_cool = true;
        }
        demand = st.proxy_cool;
    } else {  // Blind
        demand = true;
    }

    // ---- protections (§2.4) ----
    // 1. fin lockout
    if (st.fin_fault) {
        st.defrost = false;
    } else if (fin_ok) {
        if (!st.defrost && fin <= (float)s.fin_cutoff) { st.defrost = true; st.near_box = false; }
        if (st.defrost) {
            bool near = box_ok && std::fabs(fin - in.box_c) <= NEAR_BOX_C;
            if (near && !st.near_box) st.near_box_since = now;
            st.near_box = near;
            if (fin >= (float)s.fin_recover || (near && now - st.near_box_since >= NEAR_BOX_DEFROST_MS)) {
                st.defrost = false; st.near_box = false;
            }
        }
    }  // fin invalid but not yet faulted: hold defrost as-is

    bool want = demand && !st.defrost;
    uint32_t hold_s = 0;
    bool duty_rest = false, minoff_hold = false;

    // 2. timed-duty backstop, only while the fin sensor is faulted
    if (st.fin_fault) {
        if (!st.duty_active) { st.duty_active = true; st.duty_start_ms = now; }
        uint32_t win = (uint32_t)s.maxrun * 60000u;
        uint32_t pos = (now - st.duty_start_ms) % win;
        uint32_t on = (uint32_t)((uint64_t)win * (uint32_t)s.dutypercent / 100u);
        uint32_t minrun = (uint32_t)s.minruntime * 1000u;
        if (minrun > 0 && on > 0 && on < minrun) on = minrun;
        if (on > win) on = win;
        if (want && pos >= on) { want = false; duty_rest = true; hold_s = sec_left(pos, win); }
    } else {
        st.duty_active = false;
    }

    // 3. minruntime (defrost may break it)
    const uint32_t minrun_ms = (uint32_t)s.minruntime * 1000u;
    if (st.relay && !want && !st.defrost && since_relay < minrun_ms) want = true;

    // 4. minofftime (counted from boot)
    const uint32_t minoff_ms = (uint32_t)s.minofftime * 60000u;
    if (!st.relay && want && since_relay < minoff_ms) {
        want = false; minoff_hold = true; hold_s = sec_left(since_relay, minoff_ms);
    }

    if (want != st.relay) { st.relay = want; st.relay_age_ms = 0; }
    const uint32_t in_state_ms = st.relay_age_ms;

    // ---- no-response (§2.9) ----
    if (!st.fin_fault && st.relay && st.compressor == 0) {
        if (!st.nr_timing) { st.nr_timing = true; st.nr_since = now; }
        if (now - st.nr_since >= NO_RESPONSE_MS) st.no_response = true;
    } else {
        st.nr_timing = false;
        st.no_response = false;
    }

    // ---- calibration (§2.7) ----
    if (st.cal_active) {
        if (!st.cal_started) { st.cal_started = true; st.cal_started_ms = now; }
        if (now - st.cal_started_ms >= CAL_TIMEOUT_MS) st.cal_active = false;
    }
    if (st.cal_active && sampled) {
        bool steady = !st.relay && in_state_ms >= settle_ms && st.compressor == 0 && box_ok && fin_ok;
        if (!steady) {
            st.stab_n = 0;
        } else {
            st.stab_box[st.stab_head] = in.box_c;
            st.stab_ohms[st.stab_head] = ohms;
            st.stab_head = (st.stab_head + 1) % CAL_STAB_N;
            if (st.stab_n < CAL_STAB_N) st.stab_n++;
            if (st.stab_n == CAL_STAB_N) {
                float bmin = st.stab_box[0], bmax = bmin, omin = st.stab_ohms[0], omax = omin;
                for (int i = 1; i < CAL_STAB_N; i++) {
                    bmin = std::fmin(bmin, st.stab_box[i]); bmax = std::fmax(bmax, st.stab_box[i]);
                    omin = std::fmin(omin, st.stab_ohms[i]); omax = std::fmax(omax, st.stab_ohms[i]);
                }
                bool stable = bmax - bmin <= CAL_STAB_TEMP_C && omax - omin <= CAL_STAB_OHMS_FRAC * omin;
                bool far = true;
                for (int i = 0; i < st.cal_n; i++)
                    if (std::fabs(st.cal_pts[i].temp_c - in.box_c) < CAL_MIN_SEP_C) far = false;
                if (stable && far && st.cal_n < CAL_MAX_POINTS) {
                    st.cal_pts[st.cal_n++] = CalPoint{ohms, in.box_c};
                }
            }
        }
        if (st.cal_n >= CAL_MIN_POINTS) {
            float lo = st.cal_pts[0].temp_c, hi = lo;
            for (int i = 1; i < st.cal_n; i++) { lo = std::fmin(lo, st.cal_pts[i].temp_c); hi = std::fmax(hi, st.cal_pts[i].temp_c); }
            if (hi - lo >= CAL_MIN_SPAN_C) {
                CalFit f = fit_beta(st.cal_pts, st.cal_n);
                if (f.ok) cal.last_err = f.err_c;   // degenerate: keep the previous error
                if (f.ok && f.beta >= CAL_BETA_MIN && f.beta <= CAL_BETA_MAX && f.err_c <= CAL_MAX_ERR_C) {
                    cal.beta = f.beta; cal.r0 = f.r0; cal.calibrated = true;
                }
                st.cal_active = false;
            }
        }
    }

    // ---- run state, hold_s (§2.6) ----
    RunState rs;
    if (st.relay) {
        rs = RunState::Cooling;
        hold_s = sec_left(in_state_ms, minrun_ms);
    } else if (st.defrost) {
        rs = RunState::Defrost; hold_s = 0;
    } else if (minoff_hold) {
        rs = RunState::Wait;
    } else if (duty_rest) {
        rs = RunState::Rest;
    } else if (settle_pending) {
        rs = RunState::Rest; hold_s = sec_left(in_state_ms, settle_ms);
    } else {
        rs = RunState::Idle; hold_s = 0;
    }

    // ---- outputs ----
    o.relay = st.relay;
    o.box_valid = box_fresh;
    o.mode = mode;
    o.override_src = sw && ln ? OverrideSrc::Both : sw ? OverrideSrc::Switch : ln ? OverrideSrc::Link : OverrideSrc::None;
    o.state = rs;
    o.cool_call = demand;
    o.defrost = st.defrost;
    o.sht_fault = st.sht_fault;
    o.fin_fault = st.fin_fault;
    o.link_lost = st.link_lost;
    o.no_response = st.no_response;
    o.compressor = st.compressor;
    o.fin_c = fin;
    o.fin_ohms = fin_ok ? ohms : NAN;
    o.fin_slope = slope;
    o.run_s = st.relay ? in_state_ms / 1000 : 0;
    o.off_s = st.relay ? 0 : in_state_ms / 1000;
    o.hold_s = hold_s;
    o.cal_active = st.cal_active;
    o.cal_points = st.cal_n;
    {
        float lo = 0, hi = 0;
        for (int i = 0; i < st.cal_n; i++) {
            float t = st.cal_pts[i].temp_c;
            if (i == 0 || t < lo) lo = t;
            if (i == 0 || t > hi) hi = t;
        }
        o.cal_span = hi - lo;
    }
    if (o.sht_fault || o.fin_fault || o.no_response) o.led = Led::Fast;
    else if (o.defrost) o.led = Led::Slow;
    else if (o.relay) o.led = Led::Solid;
    else o.led = Led::Off;

    uint32_t sig = (uint32_t)o.relay | (uint32_t)o.state << 1 | (uint32_t)o.mode << 4 |
                   (uint32_t)o.override_src << 7 | (uint32_t)o.sht_fault << 9 |
                   (uint32_t)o.fin_fault << 10 | (uint32_t)o.defrost << 11 |
                   (uint32_t)(o.compressor + 1) << 12 | (uint32_t)o.no_response << 14 |
                   (uint32_t)o.cal_active << 15 | (uint32_t)o.cal_points << 16;
    o.publish_now = sig != st.sig;
    st.sig = sig;
    return o;
}

}  // namespace cooler
