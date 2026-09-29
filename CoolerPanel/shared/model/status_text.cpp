#include "status_text.h"
#include "cooler_state.h"
#include <cstdio>
#include <cstring>

const char* fmt_dur(uint32_t secs, char* out, size_t cap) {
    if (secs < 60)        std::snprintf(out, cap, "%us", (unsigned)secs);
    else if (secs < 3600) std::snprintf(out, cap, "%um", (unsigned)(secs / 60));
    else                  std::snprintf(out, cap, "%uh %02um", (unsigned)(secs / 3600),
                                        (unsigned)((secs % 3600) / 60));
    return out;
}

static bool is(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

const char* fmt_mode_label(const CoolerState& s) {
    if (!s.valid || !s.mode[0])       return "--";
    if (is(s.mode, "normal"))         return "NORMAL";
    if (is(s.mode, "override"))       return "OVERRIDE";
    if (is(s.mode, "fin-proxy"))      return "FIN PROXY";
    if (is(s.mode, "override-proxy")) return "OVR + FIN PROXY";
    if (is(s.mode, "blind"))          return "BLIND TIMER";
    return s.mode;
}

const char* fmt_state_value(const CoolerState& s, char* out, size_t cap) {
    if (!s.valid || !s.state[0]) { std::snprintf(out, cap, "--"); return out; }
    char d[16];
    if (is(s.state, "cooling")) {
        // Inside the minimum run the relay is held closed: show the time left
        // (U+2022 bullet, present in LVGL's built-in fonts).
        if (s.hold_s) std::snprintf(out, cap, "Cooling \u2022 min run %s", fmt_dur(s.hold_s, d, sizeof(d)));
        else          std::snprintf(out, cap, "Cooling %s", fmt_dur(s.run_s, d, sizeof(d)));
    }
    else if (is(s.state, "defrost")) std::snprintf(out, cap, "Defrost %s", fmt_dur(s.off_s, d, sizeof(d)));
    else if (is(s.state, "wait"))    std::snprintf(out, cap, "Waiting %s", fmt_dur(s.hold_s, d, sizeof(d)));
    else if (is(s.state, "rest"))    {
        if (s.hold_s) std::snprintf(out, cap, "Resting %s", fmt_dur(s.hold_s, d, sizeof(d)));
        else          std::snprintf(out, cap, "Resting");
    }
    // Relay open with nothing needed: the AC's continuous fan is still moving
    // box air (the install guide sets the Frigidaire to continuous fan).
    else if (is(s.state, "idle"))    std::snprintf(out, cap, "Fan only %s", fmt_dur(s.off_s, d, sizeof(d)));
    else                             std::snprintf(out, cap, "%s", s.state);
    return out;
}

const char* fmt_state_key(const CoolerState& s, char* out, size_t cap) {
    const char* m = fmt_mode_label(s);
    if (s.valid && is(s.state, "rest")) {
        // A rest is either the timed backup duty (fin sensor failed) or the
        // fin-proxy settle before the coil can be read as box temperature.
        const char* why = s.fin_fault ? "BACKUP DUTY"
                        : std::strstr(s.mode, "proxy") ? "SETTLE" : nullptr;
        if (why) std::snprintf(out, cap, "%s - %s", m, why);
        else     std::snprintf(out, cap, "%s", m);
    } else {
        std::snprintf(out, cap, "%s", m);
    }
    return out;
}

const char* fmt_compressor(const CoolerState& s) {
    if (!s.valid || s.compressor < 0) return "--";
    if (s.compressor == 1) return "Running";
    return s.relay ? "Starting" : "Stopped";
}

const char* fmt_fincal(const CoolerState& s, char* out, size_t cap) {
    if (s.cal_active)   std::snprintf(out, cap, "calibrating %d pts %.1fC", s.cal_points, (double)s.cal_span);
    else if (s.fin_cal) std::snprintf(out, cap, "fin cal ok");
    else                std::snprintf(out, cap, "fin uncalibrated");
    return out;
}

SwitchChip fmt_switch_chip(const CoolerState& s) {
    if (!s.valid || !s.override_src[0])      return {"--", false};
    if (is(s.override_src, "switch") || is(s.override_src, "both")) return {"SWITCH ON", true};
    if (is(s.override_src, "link"))          return {"LINK LOST", true};
    return {"SWITCH OFF", false};
}
