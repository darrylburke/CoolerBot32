// dehum_logic.h -- every control decision for the dehumidifier relay.
//
// Pure C++17: no ESPHome or Arduino. The ESPHome YAML calls step() once a
// second (and right after any MQTT message) and drives the relay from the
// returned Outputs. Host tests compile this same header (dehumidifier/tests).
//
// Spec: docs/superpowers/specs/2026-10-28-dehumidifier-controller-design.md
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>

namespace dehum {

constexpr uint32_t LINK_LOST_MS = 60000;   // broker gone this long -> relay off (spec 2.4)
constexpr uint32_t WANT_FRESH_MS = 90000;  // a Cooler want older than this is no want (spec 2.2)
constexpr uint32_t RH_STALE_MS = 30000;    // a local humidity sensor reading older than this is no reading
constexpr int RH_BAND = 3;                 // %RH: humid at >= dehum_rh, not below dehum_rh - 3

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

// Persisted as one record (spec 3.1). The mode is stored too.
struct Settings {
    int min_run = 10;    // min  0 .. 60
    int timer_h = 2;     // h    1 .. 24
    int cycle_on = 30;   // min  max(1, min_run) .. 240
    int cycle_off = 30;  // min  1 .. 240
    int dehum_rh = 65;   // %RH 30 .. 95, AUTO threshold on the local humidity sensor
    Mode mode = Mode::Auto;
};

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// min_run first: cycle_on's floor tracks it, so raising min_run bumps cycle_on.
inline void clamp_settings(Settings& s) {
    s.min_run = clampi(s.min_run, 0, 60);
    s.timer_h = clampi(s.timer_h, 1, 24);
    s.cycle_on = clampi(s.cycle_on, s.min_run > 1 ? s.min_run : 1, 240);
    s.cycle_off = clampi(s.cycle_off, 1, 240);
    s.dehum_rh = clampi(s.dehum_rh, 30, 95);
    if ((uint8_t)s.mode > (uint8_t)Mode::Cycle) s.mode = Mode::Auto;
}

struct State {
    bool init = false;
    uint32_t last_now = 0;

    bool ever_connected = false;   // relay stays off until the first broker connect
    uint32_t disc_ms = 0;          // continuous time disconnected
    bool link_lost = false;

    bool has_want = false;
    bool want = false;
    uint32_t want_age_ms = 0;

    uint32_t mode_ms = 0;          // TIMER: elapsed; CYCLE: elapsed in this phase
    bool cycle_on_phase = true;

    bool humid = false;            // local RH latch (hysteresis)

    bool relay = false;
    uint32_t relay_age_ms = 0;     // time in the current relay state

    bool sig_init = false;
    uint32_t sig = 0;
};

struct Cmd {
    std::optional<int> min_run, timer_h, cycle_on, cycle_off, dehum_rh;
    std::optional<Mode> mode;
};

struct Inputs {
    bool mqtt_connected = false;
    float rh = NAN;              // local humidity sensor humidity, NAN if none
    uint32_t rh_age_ms = UINT32_MAX; // ms since the humidity sensor last delivered a reading
};

struct Outputs {
    bool relay = false;
    Mode mode = Mode::Auto;
    int want = -1;                 // -1 = none within 90 s, else 0/1
    bool link_lost = false;
    uint32_t hold_s = 0;           // min_run seconds left while relay on, else 0
    uint32_t remain_s = 0;         // TIMER: left; CYCLE: left in phase; else 0
    int phase = -1;                // CYCLE: 1 on / 0 off; else -1
    bool rh_valid = false;         // fresh local reading
    bool humid = false;            // local RH at/above dehum_rh (with hysteresis)
    uint32_t run_s = 0;            // seconds in current relay state
    bool settings_changed = false; // TIMER expired -> mode became Auto; caller saves
    bool publish_now = false;      // a published field other than the *_s counters changed
};

inline void sat_add(uint32_t& a, uint32_t d) { a = (a > UINT32_MAX - d) ? UINT32_MAX : a + d; }
inline uint32_t ceil_s(uint32_t ms) { return (ms + 999) / 1000; }

inline void restart_mode_timers(State& st) { st.mode_ms = 0; st.cycle_on_phase = true; }

// Settings first (then clamp), then mode. A present mode always restarts
// TIMER/CYCLE, even when it is unchanged. Returns true if Settings changed.
inline bool apply_cmd(const Cmd& c, Settings& s, State& st) {
    const Settings before = s;
    if (c.min_run) s.min_run = *c.min_run;
    if (c.timer_h) s.timer_h = *c.timer_h;
    if (c.cycle_on) s.cycle_on = *c.cycle_on;
    if (c.cycle_off) s.cycle_off = *c.cycle_off;
    if (c.dehum_rh) s.dehum_rh = *c.dehum_rh;
    if (c.mode) s.mode = *c.mode;
    clamp_settings(s);
    if (c.mode) restart_mode_timers(st);
    return s.min_run != before.min_run || s.timer_h != before.timer_h ||
           s.cycle_on != before.cycle_on || s.cycle_off != before.cycle_off ||
           s.dehum_rh != before.dehum_rh || s.mode != before.mode;
}

inline void on_want(State& st, bool want) { st.has_want = true; st.want = want; st.want_age_ms = 0; }

// One control tick.
inline Outputs step(const Inputs& in, Settings& s, State& st, uint32_t now) {
    Outputs o;
    const uint32_t dt = st.init ? now - st.last_now : 0;   // unsigned: wrap-safe
    st.init = true;
    st.last_now = now;

    if (st.has_want) sat_add(st.want_age_ms, dt);
    sat_add(st.relay_age_ms, dt);

    bool restarted = false;
    if (in.mqtt_connected) {
        if (!st.ever_connected) { st.ever_connected = true; restart_mode_timers(st); restarted = true; }
        st.disc_ms = 0;
        st.link_lost = false;
    } else if (st.ever_connected) {
        sat_add(st.disc_ms, dt);
        // A want from before the outage must not restart AUTO on reconnect:
        // forget it, and wait for the Cooler's next message (<= 30 s).
        if (st.disc_ms >= LINK_LOST_MS) { st.link_lost = true; st.has_want = false; }
    }
    // TIMER/CYCLE count from the first connect, and keep counting through a broker loss.
    if (st.ever_connected && !restarted) sat_add(st.mode_ms, dt);

    const bool want_fresh = st.has_want && st.want_age_ms < WANT_FRESH_MS;

    o.rh_valid = !std::isnan(in.rh) && in.rh_age_ms < RH_STALE_MS;
    if (!o.rh_valid) st.humid = false;
    else if (in.rh >= (float)s.dehum_rh) st.humid = true;
    else if (in.rh < (float)(s.dehum_rh - RH_BAND)) st.humid = false;
    o.humid = st.humid;
    o.want = want_fresh ? (st.want ? 1 : 0) : -1;

    if (s.mode == Mode::Timer) {
        const uint32_t limit = (uint32_t)s.timer_h * 3600000u;
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

    bool desired = false;
    switch (s.mode) {
        case Mode::Auto: desired = want_fresh && st.want && st.humid; break;
        case Mode::On: case Mode::Timer: desired = true; break;
        case Mode::Cycle: desired = st.cycle_on_phase; break;
        default: desired = false; break;
    }

    const uint32_t min_run_ms = (uint32_t)s.min_run * 60000u;
    const bool holding = st.relay && st.relay_age_ms < min_run_ms;
    bool next;
    if (!st.ever_connected || st.link_lost || s.mode == Mode::Off) next = false;
    else next = desired || holding;
    if (next != st.relay) { st.relay = next; st.relay_age_ms = 0; }

    o.relay = st.relay;
    o.mode = s.mode;
    o.link_lost = st.link_lost;
    o.hold_s = (st.relay && st.relay_age_ms < min_run_ms) ? ceil_s(min_run_ms - st.relay_age_ms) : 0;
    o.run_s = st.relay_age_ms / 1000;

    const uint32_t sig = (o.relay ? 1u : 0u) | ((uint32_t)o.mode << 1) | ((uint32_t)(o.want + 1) << 4) |
                         ((o.link_lost ? 1u : 0u) << 6) | ((uint32_t)(o.phase + 1) << 7) |
                         ((o.humid ? 1u : 0u) << 9) | ((o.rh_valid ? 1u : 0u) << 10);
    o.publish_now = !st.sig_init || sig != st.sig;
    st.sig_init = true;
    st.sig = sig;
    return o;
}

}  // namespace dehum
