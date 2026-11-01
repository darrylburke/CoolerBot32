// dehum_logic.h -- every control decision for the dehumidifier relays.
//
// Pure C++17: no ESPHome or Arduino. The ESPHome YAML calls step() once a
// second (and right after any MQTT message) and drives both relays from the
// returned Outputs. Host tests compile this same header (dehumidifier/tests).
//
// Spec: docs/superpowers/specs/2026-10-28-dehumidifier-controller-design.md,
// AUTO rules: docs/superpowers/specs/2026-10-31-dehum-rules-and-temp-humidity-tab-design.md
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>

namespace dehum {

constexpr uint32_t LINK_LOST_MS = 60000;   // broker gone this long -> relays off
constexpr uint32_t RH_STALE_MS = 30000;    // an HDC1080 reading older than this is no reading
constexpr int RH_BAND = 3;                 // %RH hysteresis band for humidity rules
constexpr int TEMP_BAND = 1;               // degC hysteresis band for temperature rules
constexpr int NUM_RULES = 4;

enum class Mode : uint8_t { Auto, On, Off, Timer, Cycle };

inline const char* mode_str(Mode m) {
    switch (m) {
        case Mode::On: return "on";
        case Mode::Off: return "off";
        case Mode::Timer: return "timer";
        case Mode::Cycle: return "cycle";
        default: return "auto";
    }
}

inline std::optional<Mode> parse_mode(const char* s) {
    if (s == nullptr) return std::nullopt;
    for (Mode m : {Mode::Auto, Mode::On, Mode::Off, Mode::Timer, Mode::Cycle})
        if (std::strcmp(s, mode_str(m)) == 0) return m;
    return std::nullopt;
}

// One AUTO rule: "if <sensor> is <cmp> <value>, turn relay <relay> <action>".
// Plain ints so the record persists as-is and the MQTT keys map 1:1.
struct Rule {
    int en = 0;      // 1 enabled, 0 disabled
    int sensor = 1;  // 0 temperature, 1 humidity
    int cmp = 1;     // 0 below, 1 above
    int value = 65;  // whole degC (-20 .. 50) or %RH (0 .. 100)
    int relay = 1;   // 1 dehumidifier (GPIO16), 2 spare (GPIO23)
    int action = 1;  // 1 turn on, 0 turn off
    bool operator==(const Rule& o) const {
        return en == o.en && sensor == o.sensor && cmp == o.cmp && value == o.value &&
               relay == o.relay && action == o.action;
    }
    bool operator!=(const Rule& o) const { return !(*this == o); }
};

// Persisted as one record. The mode is stored too.
struct Settings {
    int min_run = 10;    // min  0 .. 60
    int timer_min = 120; // min  1 .. 1440 (24 h)
    int cycle_on = 30;   // min  max(1, min_run) .. 240
    int cycle_off = 30;  // min  1 .. 240
    Rule rules[NUM_RULES] = {
        {1, 1, 1, 65, 1, 1},   // humidity above 65 -> relay 1 on
        {0, 1, 0, 55, 1, 0},   // humidity below 55 -> relay 1 off
        {0, 0, 1, 10, 2, 1},   // temp above 10 -> relay 2 on
        {0, 0, 0, 2, 2, 1},    // temp below 2 -> relay 2 on
    };
    Mode mode = Mode::Auto;
};

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Sensor first: the value's bounds follow it.
inline void clamp_rule(Rule& r) {
    r.en = clampi(r.en, 0, 1);
    r.sensor = clampi(r.sensor, 0, 1);
    r.cmp = clampi(r.cmp, 0, 1);
    r.value = r.sensor == 0 ? clampi(r.value, -20, 50) : clampi(r.value, 0, 100);
    r.relay = clampi(r.relay, 1, 2);
    r.action = clampi(r.action, 0, 1);
}

// min_run first: cycle_on's floor tracks it, so raising min_run bumps cycle_on.
inline void clamp_settings(Settings& s) {
    s.min_run = clampi(s.min_run, 0, 60);
    s.timer_min = clampi(s.timer_min, 1, 1440);
    s.cycle_on = clampi(s.cycle_on, s.min_run > 1 ? s.min_run : 1, 240);
    s.cycle_off = clampi(s.cycle_off, 1, 240);
    for (Rule& r : s.rules) clamp_rule(r);
    if ((uint8_t)s.mode > (uint8_t)Mode::Cycle) s.mode = Mode::Auto;
}

struct State {
    bool init = false;
    uint32_t last_now = 0;

    bool ever_connected = false;   // relays stay off until the first broker connect
    uint32_t disc_ms = 0;          // continuous time disconnected
    bool link_lost = false;

    uint32_t mode_ms = 0;          // TIMER: elapsed; CYCLE: elapsed in this phase
    bool cycle_on_phase = true;

    bool match[NUM_RULES] = {};    // per-rule latch (hysteresis)

    bool relay = false;
    uint32_t relay_age_ms = 0;     // time in the current relay 1 state
    bool relay2 = false;
    uint32_t relay2_age_ms = 0;    // time in the current relay 2 state

    bool sig_init = false;
    uint32_t sig = 0;
};

struct RuleCmd {
    std::optional<int> en, sensor, cmp, value, relay, action;
};

struct Cmd {
    std::optional<int> min_run, timer_min, cycle_on, cycle_off;
    RuleCmd rules[NUM_RULES];
    std::optional<Mode> mode;
};

struct Inputs {
    bool mqtt_connected = false;
    float temp = NAN;            // HDC1080 temperature degC, NAN if none
    float rh = NAN;              // HDC1080 humidity %RH, NAN if none
    uint32_t rh_age_ms = UINT32_MAX;   // ms since the HDC1080 last delivered humidity
    uint32_t temp_age_ms = UINT32_MAX; // ms since it last delivered temperature (the
                                       // driver can deliver one without the other)
};

struct Outputs {
    bool relay = false;            // relay 1, dehumidifier
    bool relay2 = false;           // relay 2, spare
    Mode mode = Mode::Auto;
    bool link_lost = false;
    uint32_t hold_s = 0;           // relay 1: min_run seconds left while on, else 0
    uint32_t hold2_s = 0;          // relay 2: same
    uint32_t remain_s = 0;         // TIMER: left; CYCLE: left in phase; else 0
    int phase = -1;                // CYCLE: 1 on / 0 off; else -1
    bool rh_valid = false;         // fresh humidity reading
    bool temp_valid = false;       // fresh temperature reading
    bool match[NUM_RULES] = {};    // rule enabled and met (with hysteresis)
    uint32_t run_s = 0;            // seconds in current relay 1 state
    bool settings_changed = false; // TIMER expired -> mode became Auto; caller saves
    bool publish_now = false;      // a published field other than the *_s counters changed
};

inline void sat_add(uint32_t& a, uint32_t d) { a = (a > UINT32_MAX - d) ? UINT32_MAX : a + d; }
inline uint32_t ceil_s(uint32_t ms) { return (ms + 999) / 1000; }

inline void restart_mode_timers(State& st) { st.mode_ms = 0; st.cycle_on_phase = true; }

inline void apply_rule_cmd(const RuleCmd& c, Rule& r) {
    if (c.en) r.en = *c.en;
    if (c.sensor) r.sensor = *c.sensor;
    if (c.cmp) r.cmp = *c.cmp;
    if (c.value) r.value = *c.value;
    if (c.relay) r.relay = *c.relay;
    if (c.action) r.action = *c.action;
}

// Settings first (then clamp), then mode. A present mode always restarts
// TIMER/CYCLE, even when it is unchanged. A changed rule loses its latch, so
// it is judged afresh from its threshold. Returns true if Settings changed.
inline bool apply_cmd(const Cmd& c, Settings& s, State& st) {
    const Settings before = s;
    if (c.min_run) s.min_run = *c.min_run;
    if (c.timer_min) s.timer_min = *c.timer_min;
    if (c.cycle_on) s.cycle_on = *c.cycle_on;
    if (c.cycle_off) s.cycle_off = *c.cycle_off;
    for (int i = 0; i < NUM_RULES; i++) apply_rule_cmd(c.rules[i], s.rules[i]);
    if (c.mode) s.mode = *c.mode;
    clamp_settings(s);
    if (c.mode) restart_mode_timers(st);
    bool changed = s.min_run != before.min_run || s.timer_min != before.timer_min ||
                   s.cycle_on != before.cycle_on || s.cycle_off != before.cycle_off ||
                   s.mode != before.mode;
    for (int i = 0; i < NUM_RULES; i++) {
        if (s.rules[i] != before.rules[i]) { st.match[i] = false; changed = true; }
    }
    return changed;
}

// Hysteresis: above latches on at x >= value, off at x < value - band;
// below latches on at x <= value, off at x > value + band.
inline bool judge_rule(const Rule& r, bool latched, bool valid, float x) {
    if (!r.en || !valid) return false;
    const float v = (float)r.value;
    const float band = (float)(r.sensor == 0 ? TEMP_BAND : RH_BAND);
    if (r.cmp == 1) {
        if (x >= v) return true;
        if (x < v - band) return false;
    } else {
        if (x <= v) return true;
        if (x > v + band) return false;
    }
    return latched;
}

// Next state of one relay: forced off, else desired or held by min_run.
inline void drive_relay(bool& relay, uint32_t& age_ms, bool desired, bool force_off, uint32_t min_run_ms) {
    const bool holding = relay && age_ms < min_run_ms;
    const bool next = !force_off && (desired || holding);
    if (next != relay) { relay = next; age_ms = 0; }
}

inline uint32_t hold_left_s(bool relay, uint32_t age_ms, uint32_t min_run_ms) {
    return (relay && age_ms < min_run_ms) ? ceil_s(min_run_ms - age_ms) : 0;
}

// One control tick.
inline Outputs step(const Inputs& in, Settings& s, State& st, uint32_t now) {
    Outputs o;
    const uint32_t dt = st.init ? now - st.last_now : 0;   // unsigned: wrap-safe
    st.init = true;
    st.last_now = now;

    sat_add(st.relay_age_ms, dt);
    sat_add(st.relay2_age_ms, dt);

    bool restarted = false;
    if (in.mqtt_connected) {
        if (!st.ever_connected) { st.ever_connected = true; restart_mode_timers(st); restarted = true; }
        st.disc_ms = 0;
        st.link_lost = false;
    } else if (st.ever_connected) {
        sat_add(st.disc_ms, dt);
        if (st.disc_ms >= LINK_LOST_MS) st.link_lost = true;
    }
    // TIMER/CYCLE count from the first connect, and keep counting through a broker loss.
    if (st.ever_connected && !restarted) sat_add(st.mode_ms, dt);

    o.rh_valid = in.rh_age_ms < RH_STALE_MS && !std::isnan(in.rh);
    o.temp_valid = in.temp_age_ms < RH_STALE_MS && !std::isnan(in.temp);

    // Rules are judged in every mode (the app shows "met"), but only AUTO acts on them.
    bool want_on[2] = {false, false}, want_off[2] = {false, false};
    for (int i = 0; i < NUM_RULES; i++) {
        const Rule& r = s.rules[i];
        const bool valid = r.sensor == 0 ? o.temp_valid : o.rh_valid;
        st.match[i] = judge_rule(r, st.match[i], valid, r.sensor == 0 ? in.temp : in.rh);
        o.match[i] = st.match[i];
        if (st.match[i]) (r.action ? want_on : want_off)[r.relay - 1] = true;
    }

    if (s.mode == Mode::Timer) {
        const uint32_t limit = (uint32_t)s.timer_min * 60000u;
        if (st.mode_ms >= limit) { s.mode = Mode::Auto; o.settings_changed = true; }
        else o.remain_s = ceil_s(limit - st.mode_ms);
    }
    if (s.mode == Mode::Cycle) {
        for (;;) {
            const uint32_t len = (uint32_t)(st.cycle_on_phase ? s.cycle_on : s.cycle_off) * 60000u;
            if (st.mode_ms < len) { o.remain_s = ceil_s(len - st.mode_ms); break; }
            st.mode_ms -= len;
            st.cycle_on_phase = !st.cycle_on_phase;
        }
        o.phase = st.cycle_on_phase ? 1 : 0;
    }

    // AUTO: off wins. ON/TIMER/CYCLE drive relay 1 only; relay 2 stays off.
    bool desired = false, desired2 = false;
    switch (s.mode) {
        case Mode::Auto:
            desired = want_on[0] && !want_off[0];
            desired2 = want_on[1] && !want_off[1];
            break;
        case Mode::On: case Mode::Timer: desired = true; break;
        case Mode::Cycle: desired = st.cycle_on_phase; break;
        default: break;
    }

    const uint32_t min_run_ms = (uint32_t)s.min_run * 60000u;
    const bool force_off = !st.ever_connected || st.link_lost || s.mode == Mode::Off;
    drive_relay(st.relay, st.relay_age_ms, desired, force_off, min_run_ms);
    drive_relay(st.relay2, st.relay2_age_ms, desired2, force_off, min_run_ms);

    o.relay = st.relay;
    o.relay2 = st.relay2;
    o.mode = s.mode;
    o.link_lost = st.link_lost;
    o.hold_s = hold_left_s(st.relay, st.relay_age_ms, min_run_ms);
    o.hold2_s = hold_left_s(st.relay2, st.relay2_age_ms, min_run_ms);
    o.run_s = st.relay_age_ms / 1000;

    uint32_t sig = (o.relay ? 1u : 0u) | ((o.relay2 ? 1u : 0u) << 1) | ((uint32_t)o.mode << 2) |
                   ((o.link_lost ? 1u : 0u) << 5) | ((uint32_t)(o.phase + 1) << 6) |
                   ((o.rh_valid ? 1u : 0u) << 8) | ((o.temp_valid ? 1u : 0u) << 9);
    for (int i = 0; i < NUM_RULES; i++) sig |= (o.match[i] ? 1u : 0u) << (10 + i);
    o.publish_now = !st.sig_init || sig != st.sig;
    st.sig_init = true;
    st.sig = sig;
    return o;
}

}  // namespace dehum
