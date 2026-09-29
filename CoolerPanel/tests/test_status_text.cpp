#include <doctest/doctest.h>
#include "status_text.h"
#include "cooler_state.h"
#include <cstdio>
#include <string>

static CoolerState st(const char* mode, const char* state) {
    CoolerState s;
    s.valid = true;
    std::snprintf(s.mode, sizeof(s.mode), "%s", mode);
    std::snprintf(s.state, sizeof(s.state), "%s", state);
    return s;
}

TEST_CASE("fmt_dur picks seconds, minutes, or hours") {
    char b[16];
    CHECK(std::string(fmt_dur(0, b, sizeof(b))) == "0s");
    CHECK(std::string(fmt_dur(59, b, sizeof(b))) == "59s");
    CHECK(std::string(fmt_dur(60, b, sizeof(b))) == "1m");
    CHECK(std::string(fmt_dur(3599, b, sizeof(b))) == "59m");
    CHECK(std::string(fmt_dur(3900, b, sizeof(b))) == "1h 05m");
}

TEST_CASE("mode labels") {
    CHECK(std::string(fmt_mode_label(st("normal", "idle"))) == "NORMAL");
    CHECK(std::string(fmt_mode_label(st("override", "idle"))) == "OVERRIDE");
    CHECK(std::string(fmt_mode_label(st("fin-proxy", "idle"))) == "FIN PROXY");
    CHECK(std::string(fmt_mode_label(st("override-proxy", "idle"))) == "OVR + FIN PROXY");
    CHECK(std::string(fmt_mode_label(st("blind", "idle"))) == "BLIND TIMER");
    CHECK(std::string(fmt_mode_label(CoolerState{})) == "--");
}

TEST_CASE("state value uses the timer that matters for each state") {
    char b[32];
    CoolerState s = st("normal", "cooling"); s.run_s = 142;
    CHECK(std::string(fmt_state_value(s, b, sizeof(b))) == "Cooling 2m");
    s = st("normal", "defrost"); s.off_s = 75;
    CHECK(std::string(fmt_state_value(s, b, sizeof(b))) == "Defrost 1m");
    s = st("normal", "wait"); s.hold_s = 200;
    CHECK(std::string(fmt_state_value(s, b, sizeof(b))) == "Waiting 3m");
    s = st("fin-proxy", "rest"); s.hold_s = 360;
    CHECK(std::string(fmt_state_value(s, b, sizeof(b))) == "Resting 6m");
    s = st("fin-proxy", "rest"); s.hold_s = 0;
    CHECK(std::string(fmt_state_value(s, b, sizeof(b))) == "Resting");
    s = st("normal", "idle"); s.off_s = 900;
    // Relay open, nothing needed: the AC's continuous fan is still running.
    CHECK(std::string(fmt_state_value(s, b, sizeof(b))) == "Fan only 15m");
    CHECK(std::string(fmt_state_value(CoolerState{}, b, sizeof(b))) == "--");
}

TEST_CASE("cooling inside the minimum run says so, with the time left") {
    char b[48];
    CoolerState s = st("normal", "cooling"); s.run_s = 142; s.hold_s = 38;
    CHECK(std::string(fmt_state_value(s, b, sizeof(b))) == "Cooling \u2022 min run 38s");
    s.hold_s = 0;                               // past min run: back to elapsed time
    CHECK(std::string(fmt_state_value(s, b, sizeof(b))) == "Cooling 2m");
}

TEST_CASE("state key is the mode; min run lives on the value line now") {
    char b[48];
    CoolerState s = st("normal", "cooling"); s.hold_s = 38;
    CHECK(std::string(fmt_state_key(s, b, sizeof(b))) == "NORMAL");
    s.hold_s = 0;
    CHECK(std::string(fmt_state_key(s, b, sizeof(b))) == "NORMAL");
    s = st("override", "wait"); s.hold_s = 100;
    CHECK(std::string(fmt_state_key(s, b, sizeof(b))) == "OVERRIDE");
}

TEST_CASE("compressor wording") {
    CoolerState s = st("normal", "cooling");
    s.compressor = 1;               CHECK(std::string(fmt_compressor(s)) == "Running");
    s.compressor = 0; s.relay = 1;  CHECK(std::string(fmt_compressor(s)) == "Starting");
    s.relay = 0;                    CHECK(std::string(fmt_compressor(s)) == "Stopped");
    s.compressor = -1;              CHECK(std::string(fmt_compressor(s)) == "--");
}

TEST_CASE("fin calibration summary") {
    char b[40];
    CoolerState s = st("normal", "idle");
    CHECK(std::string(fmt_fincal(s, b, sizeof(b))) == "fin uncalibrated");
    s.fin_cal = 1;
    CHECK(std::string(fmt_fincal(s, b, sizeof(b))) == "fin cal ok");
    s.cal_active = 1; s.cal_points = 3; s.cal_span = 6.2f;
    CHECK(std::string(fmt_fincal(s, b, sizeof(b))) == "calibrating 3 pts 6.2C");
}

TEST_CASE("state key names what a rest is waiting on") {
    char b[48];
    CoolerState s = st("fin-proxy", "rest"); s.hold_s = 360;
    CHECK(std::string(fmt_state_key(s, b, sizeof(b))) == "FIN PROXY - SETTLE");
    s = st("override-proxy", "rest"); s.hold_s = 60;
    CHECK(std::string(fmt_state_key(s, b, sizeof(b))) == "OVR + FIN PROXY - SETTLE");
    s = st("blind", "rest"); s.fin_fault = 1; s.hold_s = 180;
    CHECK(std::string(fmt_state_key(s, b, sizeof(b))) == "BLIND TIMER - BACKUP DUTY");
    s = st("normal", "rest"); s.fin_fault = 1; s.hold_s = 180;
    CHECK(std::string(fmt_state_key(s, b, sizeof(b))) == "NORMAL - BACKUP DUTY");
}

TEST_CASE("switch chip shows the override switch position, or a lost link") {
    CoolerState s = st("normal", "idle");
    std::snprintf(s.override_src, sizeof(s.override_src), "none");
    CHECK(std::string(fmt_switch_chip(s).text) == "SWITCH OFF");
    CHECK_FALSE(fmt_switch_chip(s).alert);
    std::snprintf(s.override_src, sizeof(s.override_src), "switch");
    CHECK(std::string(fmt_switch_chip(s).text) == "SWITCH ON");
    CHECK(fmt_switch_chip(s).alert);
    std::snprintf(s.override_src, sizeof(s.override_src), "both");   // switch closed + link lost
    CHECK(std::string(fmt_switch_chip(s).text) == "SWITCH ON");
    std::snprintf(s.override_src, sizeof(s.override_src), "link");   // switch open, network lost
    CHECK(std::string(fmt_switch_chip(s).text) == "LINK LOST");
    CHECK(fmt_switch_chip(s).alert);
    CHECK(std::string(fmt_switch_chip(CoolerState{}).text) == "--");   // before any /data
    CHECK_FALSE(fmt_switch_chip(CoolerState{}).alert);
}
