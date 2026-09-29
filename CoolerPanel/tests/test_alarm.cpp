#include <doctest/doctest.h>
#include "alarm.h"
#include "cooler_state.h"
#include <cstdio>

static CoolerState ok_state() {
    CoolerState s;
    s.valid = true; s.online = true;
    // A fully healthy, fully-communicating panel has necessarily seen at
    // least one /availability message -- without this, every test below
    // that flips s.online = false to exercise ControllerOffline would
    // instead exercise "we've never heard an availability message at all"
    // (Step 4's safety fix), which is a different condition and stays
    // silent by design (see the two new test cases at the bottom of this
    // file).
    s.availability_seen = true;
    s.coolerset = 4; s.range = 2; s.temp = 4.0f; s.temp_valid = true;
    s.sht_fault = 0; s.last_rx_epoch = 1000;
    return s;
}

// Advance the clock WITH a fresh /data arrival. ControllerSilent outranks
// almost everything, so without refreshing last_rx_epoch it would fire
// incidentally in every test that advances time, masking the condition
// actually under test. The silence test below deliberately does not use this.
// Alarm-LOGIC tests pin their own thresholds. The shipped AlarmCfg defaults
// are a product-calibration decision (retuned against this cooler's measured
// pull-down curve) and must be free to change without breaking these.
static Alarms make_alarms(int over_c = 5, int over_s = 3600) {
    Alarms a;
    AlarmCfg c;                 // silent_s / holdoff_s keep their defaults
    c.over_c = over_c;
    c.over_s = over_s;
    a.configure(c);
    return a;
}

static void tick(Alarms& a, CoolerState& s, int64_t now) {
    s.last_rx_epoch = now;
    a.update(s, now);
}

TEST_CASE("healthy state raises nothing") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    tick(a, s, 1000);
    CHECK(a.active(1000) == AlarmId::None);
    CHECK_FALSE(a.any_latched());
}

TEST_CASE("offline availability fires immediately") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.online = false;
    tick(a, s, 1000);
    CHECK(a.active(1000) == AlarmId::ControllerOffline);
}

TEST_CASE("silence fires only after the timeout") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.last_rx_epoch = 1000;          // deliberately NOT refreshed
    a.update(s, 1299);
    CHECK(a.active(1299) == AlarmId::None);
    a.update(s, 1301);
    CHECK(a.active(1301) == AlarmId::ControllerSilent);
}

TEST_CASE("sensor fault fires immediately") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.sht_fault = 1;
    tick(a, s, 1000);
    CHECK(a.active(1000) == AlarmId::BoxSensorFault);
}

TEST_CASE("not-keeping-up needs 5C over for 60 min continuously") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.temp = 12.0f;                  // coolerset 4 + range 2 + 5 = 11 threshold
    tick(a, s, 1000);
    CHECK(a.active(1000) == AlarmId::None);        // clock just started
    tick(a, s, 4599);
    CHECK(a.active(4599) == AlarmId::None);        // 3599 s elapsed
    tick(a, s, 4601);
    CHECK(a.active(4601) == AlarmId::NotKeepingUp);
}

TEST_CASE("dropping back under the threshold resets the timer") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.temp = 12.0f;
    tick(a, s, 1000);
    tick(a, s, 4000);
    s.temp = 5.0f;                   // recovered
    tick(a, s, 4100);
    CHECK(a.active(4100) == AlarmId::None);
    s.temp = 12.0f;                  // over again -- clock restarts from 4200
    tick(a, s, 4200);
    tick(a, s, 4200 + 3599);
    CHECK(a.active(4200 + 3599) == AlarmId::None);
}

TEST_CASE("acknowledge suppresses for the hold-off then re-fires") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.sht_fault = 1;
    tick(a, s, 1000);
    REQUIRE(a.active(1000) == AlarmId::BoxSensorFault);
    a.acknowledge(AlarmId::BoxSensorFault, 1000);     // hold-off to 2800
    CHECK(a.active(1000) == AlarmId::None);
    CHECK(a.any_latched());                        // header marker stays
    tick(a, s, 2799);
    CHECK(a.active(2799) == AlarmId::None);
    tick(a, s, 2801);
    CHECK(a.active(2801) == AlarmId::BoxSensorFault);
}

TEST_CASE("acknowledging one condition does not suppress a different one") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.sht_fault = 1;
    tick(a, s, 1000);
    a.acknowledge(AlarmId::BoxSensorFault, 1000);
    REQUIRE(a.active(1000) == AlarmId::None);

    s.online = false;                              // a DIFFERENT fault appears
    tick(a, s, 1010);
    CHECK(a.active(1010) == AlarmId::ControllerOffline);
}

TEST_CASE("a cleared condition unlatches and drops its acknowledge") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.sht_fault = 1;
    tick(a, s, 1000);
    a.acknowledge(AlarmId::BoxSensorFault, 1000);
    s.sht_fault = 0;
    tick(a, s, 1100);
    CHECK(a.active(1100) == AlarmId::None);
    CHECK_FALSE(a.any_latched());
    // Coming back is a fresh alarm, not one still inside the old hold-off.
    s.sht_fault = 1;
    tick(a, s, 1200);
    CHECK(a.active(1200) == AlarmId::BoxSensorFault);
}

TEST_CASE("a dead cooler we have never had data from still alarms") {
    Alarms a;
    CoolerState s;                 // valid stays false -- no /data ever parsed
    s.availability_seen = true;    // but the retained LWT did arrive
    s.online = false;
    a.update(s, 1000);
    CHECK(a.active(1000) == AlarmId::ControllerOffline);
}

TEST_CASE("silence before any availability message is not an alarm") {
    Alarms a;
    CoolerState s;                 // nothing heard at all yet
    a.update(s, 100000);
    CHECK(a.active(100000) == AlarmId::None);
}

TEST_CASE("offline outranks not-keeping-up") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.temp = 12.0f;
    tick(a, s, 1000);
    tick(a, s, 4601);
    REQUIRE(a.active(4601) == AlarmId::NotKeepingUp);
    s.online = false;
    tick(a, s, 4602);
    CHECK(a.active(4602) == AlarmId::ControllerOffline);
}

TEST_CASE("fin sensor fault and no-response fire immediately, each on its own") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.fin_fault = 1;
    tick(a, s, 1000);
    CHECK(a.active(1000) == AlarmId::FinSensorFault);
    a.acknowledge(AlarmId::FinSensorFault, 1000);
    s.no_response = 1;
    tick(a, s, 1010);
    CHECK(a.active(1010) == AlarmId::NoResponse);
}

TEST_CASE("no-response outranks both sensor faults") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.sht_fault = 1; s.fin_fault = 1; s.no_response = 1;
    tick(a, s, 1000);
    CHECK(a.active(1000) == AlarmId::NoResponse);
}

TEST_CASE("a null temperature never counts as not keeping up") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.temp = 40.0f; s.temp_valid = false;
    tick(a, s, 1000);
    tick(a, s, 1000 + 7200);
    CHECK(a.active(1000 + 7200) != AlarmId::NotKeepingUp);
}

TEST_CASE("in override, not-keeping-up is measured against the fixed 5 C") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.coolerset = 20;                       // would put the threshold at 27
    std::snprintf(s.mode, sizeof(s.mode), "override");
    s.temp = 11.0f;                         // 5 + 5 = 10 threshold
    tick(a, s, 1000);
    tick(a, s, 4601);
    CHECK(a.active(4601) == AlarmId::NotKeepingUp);
}

TEST_CASE("a null reading pauses not-keeping-up; it does not restart the clock") {
    // One failed SHT30 read publishes temp:null. That must not hide a box that
    // has been warm for hours behind a fresh 6 h wait.
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.temp = 12.0f;                          // over the 11 C threshold
    tick(a, s, 1000);
    s.temp_valid = false;                    // a glitch...
    tick(a, s, 2000);
    CHECK(a.active(2000) == AlarmId::None);  // ...never fires on its own
    s.temp_valid = true;                     // ...and the warm box is back
    tick(a, s, 4601);
    CHECK(a.active(4601) == AlarmId::NotKeepingUp);   // measured from 1000
}

TEST_CASE("not-keeping-up does not fire on a stale temperature while the controller is silent") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.temp = 12.0f;
    tick(a, s, 1000);
    tick(a, s, 4000);                          // warm, still under the 3600 s window
    a.update(s, 4000 + 400);                   // no /data for 400 s (> silent_s 300)
    REQUIRE(a.active(4400) == AlarmId::ControllerSilent);
    a.acknowledge(AlarmId::ControllerSilent, 4400);
    a.update(s, 5000);                         // window elapsed, but the number is stale
    CHECK(a.active(5000) == AlarmId::None);
}
