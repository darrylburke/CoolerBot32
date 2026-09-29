#include <initializer_list>
#include "doctest/doctest.h"
#include "harness.h"

using namespace cooler;
using th::Rig;

TEST_CASE("boot: relay open, minofftime counted from boot") {
    Rig r; r.box(10.0f); r.fin(8.0f);
    r.run(5 * 60 - 1);
    CHECK_FALSE(r.out.relay);
    CHECK(r.out.state == RunState::Wait);
    CHECK(r.out.cool_call);
    r.run(2);
    CHECK(r.out.relay);
}

TEST_CASE("normal thermostat: strict thresholds and hold band") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.fin(8.0f);
    r.box(6.0f); r.tick(); CHECK_FALSE(r.out.relay);       // 6 is not > 4+2
    r.box(6.1f); r.tick(); CHECK(r.out.relay);
    r.box(2.0f); r.tick(); CHECK(r.out.relay);             // 2 is not < 4-2
    r.box(1.9f); r.tick(); CHECK_FALSE(r.out.relay);
    r.box(4.0f); r.tick(); CHECK_FALSE(r.out.relay);       // held off in band
    CHECK(r.out.mode == Mode::Normal);
}

TEST_CASE("override: fixed 5 / 3 thermostat ignores coolerset/range") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.s.coolerset = 20; r.fin(8.0f);
    r.in.switch_closed = true;
    r.box(4.9f); r.tick(); CHECK_FALSE(r.out.relay);
    r.box(5.0f); r.tick(); CHECK(r.out.relay);
    CHECK(r.out.mode == Mode::Override);
    CHECK(r.out.override_src == OverrideSrc::Switch);
    r.box(3.1f); r.tick(); CHECK(r.out.relay);
    r.box(3.0f); r.tick(); CHECK_FALSE(r.out.relay);
}

TEST_CASE("override via link loss: 60 s debounce both ways") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.fin(8.0f); r.box(4.0f);
    r.tick();
    r.in.mqtt_connected = false;
    r.run(59); CHECK(r.out.mode == Mode::Normal);
    r.run(2);  CHECK(r.out.mode == Mode::Override);
    CHECK(r.out.override_src == OverrideSrc::Link);
    r.in.switch_closed = true; r.tick(); CHECK(r.out.override_src == OverrideSrc::Both);
    r.in.switch_closed = false;
    r.in.mqtt_connected = true;
    r.run(59); CHECK(r.out.mode == Mode::Override);
    r.run(2);  CHECK(r.out.mode == Mode::Normal);
}

TEST_CASE("thermostat hold state carries across override -> normal") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.fin(8.0f);
    r.in.switch_closed = true;
    r.box(5.0f); r.tick(); CHECK(r.out.relay);
    r.in.switch_closed = false;
    r.box(4.0f); r.tick();                 // in normal's band: hold -> still on
    CHECK(r.out.mode == Mode::Normal);
    CHECK(r.out.relay);
}

TEST_CASE("fin lockout: entry at cutoff, exit at recover, beats override and minruntime") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 600; r.in.switch_closed = true;
    r.box(8.0f); r.fin(5.0f); r.tick(); CHECK(r.out.relay);
    r.fin(0.0f); r.tick();
    CHECK_FALSE(r.out.relay);
    CHECK(r.out.defrost);
    CHECK(r.out.state == RunState::Defrost);
    CHECK(r.out.led == Led::Slow);
    r.fin(2.9f); r.tick(); CHECK(r.out.defrost);
    r.fin(3.0f); r.tick(); CHECK_FALSE(r.out.defrost);
    CHECK(r.out.relay);
}

TEST_CASE("defrost exits via fin ~ box when the box is colder than fin_recover") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0;
    r.box(8.0f); r.fin(2.0f); r.tick();
    r.fin(-1.0f); r.tick(); CHECK(r.out.defrost);
    r.box(2.0f); r.fin(1.5f);
    r.run(119); CHECK(r.out.defrost);
    r.run(2);   CHECK_FALSE(r.out.defrost);
}

TEST_CASE("defrost does not exit via fin ~ box while the SHT30 is faulted") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0;
    r.box(8.0f); r.fin(-1.0f); r.tick(); CHECK(r.out.defrost);
    r.in.box_valid = false; r.in.box_c = 1.5f;   // stale value, reading invalid
    r.fin(1.5f);
    r.run(400);
    CHECK(r.out.sht_fault);
    CHECK(r.out.defrost);
}

TEST_CASE("sht fault after 300 s, last demand held until then, auto-recovers") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.fin(8.0f);
    r.box(7.0f); r.tick(); CHECK(r.out.relay);
    r.box_dead();
    r.run(300); CHECK_FALSE(r.out.sht_fault); CHECK(r.out.relay);
    r.run(2);   CHECK(r.out.sht_fault);
    CHECK(r.out.mode == Mode::FinProxy);
    CHECK(r.out.led == Led::Fast);
    r.box(4.0f); r.tick(); CHECK_FALSE(r.out.sht_fault);
}

TEST_CASE("fin fault debounce 10 s, backstop only while faulted") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.box(8.0f); r.fin(8.0f);
    r.tick(); CHECK(r.out.relay);
    r.fin_open();
    r.run(9);  CHECK_FALSE(r.out.fin_fault);
    r.run(2);  CHECK(r.out.fin_fault);
    CHECK(r.out.compressor == -1);
    // maxrun 10, duty 50: on for 5 min of each 10
    int on = 0;
    for (int i = 0; i < 1200; i++) { r.tick(); if (r.out.relay) on++; }
    CHECK(on == doctest::Approx(600).epsilon(0.01));
    r.fin(8.0f); r.run(11);
    CHECK_FALSE(r.out.fin_fault);
    r.run(600);
    CHECK(r.out.relay);                    // no backstop rest once healthy
}

TEST_CASE("backstop: minruntime longer than the duty portion forfeits the window") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 300; r.s.maxrun = 10; r.s.dutypercent = 10;
    r.box(8.0f); r.fin_open(); r.run(11); REQUIRE(r.out.fin_fault);
    int on = 0;
    for (int i = 0; i < 600; i++) { r.tick(); if (r.out.relay) on++; }
    CHECK(on >= 299);
    CHECK(on <= 301);
}

TEST_CASE("blind mode when both sensors fail") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0;
    r.box_dead(); r.fin_open();
    r.run(302);
    CHECK(r.out.mode == Mode::Blind);
    CHECK(r.out.cool_call);
}

TEST_CASE("fin-proxy: rest for settle, cool on settled fin >= on, stop at cutoff or maxrun") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.s.settle = 10; r.s.maxrun = 10;
    r.fin(7.0f);
    r.box(4.0f); r.tick();
    r.box_dead(); r.run(301);
    REQUIRE(r.out.mode == Mode::FinProxy);
    r.run(10 * 60 - 310);
    CHECK_FALSE(r.out.relay);
    CHECK(r.out.state == RunState::Rest);
    r.run(20);
    CHECK(r.out.relay);                          // 7 >= 4+2 after settle
    r.run(10 * 60);
    CHECK_FALSE(r.out.relay);                    // maxrun reached
    CHECK(r.out.state == RunState::Rest);
}

TEST_CASE("override-proxy uses the fixed 5 C on-threshold") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.s.settle = 2; r.s.coolerset = 20;
    r.in.switch_closed = true; r.fin(5.5f);
    r.box_dead(); r.run(302);
    REQUIRE(r.out.mode == Mode::OverrideProxy);
    r.run(130);
    CHECK(r.out.relay);
}

TEST_CASE("compressor inference: latches on falling slope, holds flat, off on rising") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.box(10.0f);
    float f = 10.0f; r.fin(f);
    r.run(60); CHECK(r.out.compressor == 0);
    for (int i = 0; i < 60; i++) { f -= 1.0f / 60; r.fin(f); r.tick(); }   // -1 C/min
    CHECK(r.out.compressor == 1);
    r.run(120); CHECK(r.out.compressor == 1);                              // flat & cold
    for (int i = 0; i < 60; i++) { f += 0.5f / 60; r.fin(f); r.tick(); }   // +0.5 C/min
    CHECK(r.out.compressor == 0);
}

TEST_CASE("compressor latches off via fin ~ box once the relay has been open 60 s") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.box(4.0f); r.fin(4.0f);
    r.st.compressor = 1; r.st.init = false;
    r.tick(); r.st.compressor = 1;
    r.run(59); CHECK(r.out.compressor == 1);
    r.run(2);  CHECK(r.out.compressor == 0);
}

TEST_CASE("no_response after 10 min closed with no compressor, clears on relay open") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.box(8.0f); r.fin(8.0f);
    r.tick(); REQUIRE(r.out.relay);
    r.run(598); CHECK_FALSE(r.out.no_response);
    r.run(2);   CHECK(r.out.no_response);
    CHECK(r.out.led == Led::Fast);
    r.box(1.0f); r.tick();
    CHECK_FALSE(r.out.relay);
    CHECK_FALSE(r.out.no_response);
}

TEST_CASE("publish_now fires on a state change and not on a quiet tick") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.box(4.0f); r.fin(4.0f);
    r.tick(); CHECK(r.out.publish_now);
    r.tick(); CHECK_FALSE(r.out.publish_now);
    r.box(7.0f); r.tick(); CHECK(r.out.publish_now);
}

TEST_CASE("calibration: collects settled points and accepts a good fit") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.s.settle = 2; r.s.coolerset = 30;
    // Real probe: Beta 3600, R0 12k. Controller starts on defaults (3950 / 10k).
    auto set_both = [&](float c) {
        r.box(c);
        float rr = 12000.0f * std::exp(3600.0f * (1.0f / (c + 273.15f) - 1.0f / T0_K));
        r.in.fin_volts = FIN_VREF * rr / (rr + FIN_R_FIXED);
    };
    set_both(11.0f); r.tick();
    cal_start(r.st);
    for (float c : {11.0f, 14.0f, 17.0f, 20.0f}) { set_both(c); r.run(10 * 60); }
    CHECK_FALSE(r.out.cal_active);
    CHECK(r.cal.calibrated);
    CHECK(r.cal.beta == doctest::Approx(3600.0f).epsilon(0.01));
    CHECK(r.cal.last_err < 0.2f);
}

TEST_CASE("calibration rejects unstable and too-close points") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.s.settle = 2; r.s.coolerset = 30;
    r.box(10.0f); r.fin(10.0f); r.tick();
    cal_start(r.st);
    for (int i = 0; i < 600; i++) { r.box(10.0f + (i % 2) * 0.5f); r.fin(10.0f); r.tick(); }
    CHECK(r.out.cal_points == 0);                          // box wobbling 0.5 C
    r.box(10.0f); r.run(600);
    CHECK(r.out.cal_points == 1);
    r.box(10.5f); r.fin(10.5f); r.run(600);
    CHECK(r.out.cal_points == 1);                          // within 1 C of a point
}

TEST_CASE("calibration fit rejected keeps previous values") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.s.settle = 2; r.s.coolerset = 30;
    r.tick(); cal_start(r.st);
    // fin ohms stay constant while box moves: nonsense curve
    for (float c : {20.0f, 17.0f, 14.0f, 11.0f}) { r.box(c); r.fin(15.0f); r.run(600); }
    CHECK_FALSE(r.out.cal_active);
    CHECK_FALSE(r.cal.calibrated);
    CHECK(r.cal.beta == 3950.0f);
}

TEST_CASE("millis() wrap: minofftime and thermostat survive the 49.7-day rollover") {
    Rig r; r.now = 0xFFFFFFFFu - 60000u;       // wraps one minute after boot
    r.box(10.0f); r.fin(8.0f);
    r.run(5 * 60 - 1);
    CHECK_FALSE(r.out.relay);                  // minofftime still counted across the wrap
    r.run(2);
    CHECK(r.out.relay);
    r.box(1.0f); r.run(200);
    CHECK_FALSE(r.out.relay);
}

TEST_CASE("settings changed mid-run apply on the next tick") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.s.settle = 2; r.s.maxrun = 30;
    r.fin(7.0f); r.box(4.0f); r.tick();
    r.box_dead(); r.run(301 + 120);
    REQUIRE(r.out.relay);                      // fin-proxy COOL, 30 min allowed
    r.run(5 * 60);
    CHECK(r.out.relay);
    r.s.maxrun = 5;                            // shrink below time already run
    r.tick();
    CHECK_FALSE(r.out.relay);
}

TEST_CASE("fin ADC noise does not latch the compressor on") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.box(4.0f);
    for (int i = 0; i < 600; i++) { r.fin(4.0f + ((i / 5) % 2 ? 0.15f : -0.15f)); r.tick(); }
    CHECK(r.out.compressor == 0);
}

TEST_CASE("a frozen SHT30 value (no fresh readings) faults after 300 s and publishes invalid") {
    // ESPHome's sht3xd keeps .state at the last good value when the sensor
    // dies -- it never goes NaN. Staleness has to come from the reading's age.
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.fin(8.0f);
    r.box(7.0f); r.tick();
    REQUIRE(r.out.relay);
    for (int i = 0; i < 29; i++) { r.in.box_age_ms = (uint32_t)i * 1000u; r.tick(); }
    CHECK(r.out.box_valid);                    // still fresh
    for (int i = 29; i < 29 + 300; i++) { r.in.box_age_ms = (uint32_t)i * 1000u; r.tick(); }
    CHECK_FALSE(r.out.box_valid);
    CHECK_FALSE(r.out.sht_fault);              // 300 s from the last FRESH reading
    for (int i = 329; i < 340; i++) { r.in.box_age_ms = (uint32_t)i * 1000u; r.tick(); }
    CHECK(r.out.sht_fault);
    CHECK(r.out.mode == Mode::FinProxy);
}

TEST_CASE("an open fin probe near the ADC ceiling faults instead of reading -40 C") {
    // The S3 ADC tops out around 3.1 V at 12 dB, below the 1 Mohm open
    // threshold, so an open probe must be caught as implausible.
    for (float v : {3.05f, 3.15f}) {
        Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.box(10.0f);
        r.in.fin_volts = v;
        r.run(11);
        CHECK(r.out.fin_fault);
        CHECK_FALSE(r.out.defrost);
        CHECK(r.out.led == Led::Fast);
    }
}

TEST_CASE("random fin noise never latches the compressor on, so no_response still fires") {
    // Relay closed, AC dead, coil still: only noise moves the fin reading.
    // Spec §2.8: the slope is taken over a full 60 s window.
    uint32_t seed = 12345;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (float)(seed >> 8) / 16777216.0f; };
    int missed = 0;
    for (int trial = 0; trial < 300; trial++) {
        Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.box(8.0f);
        for (int t = 0; t < 11 * 60; t++) { r.fin(8.0f + (rnd() - 0.5f) * 0.3f); r.tick(); }
        if (!r.out.no_response) missed++;
    }
    CHECK(missed == 0);
}

TEST_CASE("a calibration run that never reaches enough span times out after 48 h") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.s.settle = 2; r.s.coolerset = 30;
    r.box(10.0f); r.fin(10.0f); r.tick();
    cal_start(r.st);
    r.run(48u * 3600u - 10u);
    CHECK(r.out.cal_active);                   // one point, no span: still collecting
    r.run(20);
    CHECK_FALSE(r.out.cal_active);
    CHECK_FALSE(r.cal.calibrated);
    CHECK(r.cal.beta == 3950.0f);              // defaults untouched
}

// ---- final-review minors ------------------------------------------------------

TEST_CASE("calibrate:1 during an active run keeps the points already collected") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.s.settle = 2; r.s.coolerset = 30;
    r.box(10.0f); r.fin(10.0f); r.tick();
    cal_start(r.st);
    r.run(600);
    REQUIRE(r.out.cal_points == 1);
    cal_start(r.st);                           // a repeated start is not a restart
    r.tick();
    CHECK(r.out.cal_active);
    CHECK(r.out.cal_points == 1);
}

TEST_CASE("a degenerate calibration fit keeps the previous error value") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.s.settle = 2; r.s.coolerset = 30;
    r.cal.last_err = 0.4f;
    r.tick(); cal_start(r.st);
    for (float c : {20.0f, 17.0f, 14.0f, 11.0f}) { r.box(c); r.fin(15.0f); r.run(600); }
    REQUIRE_FALSE(r.out.cal_active);
    CHECK(r.cal.last_err == doctest::Approx(0.4f));
}

TEST_CASE("one bad fin sample neither resets the no-response timer nor the slope window") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.box(8.0f); r.fin(8.0f);
    r.tick(); REQUIRE(r.out.relay);
    r.run(300);
    r.fin_open(); r.tick();                    // one glitch, far short of the 10 s debounce
    CHECK(r.out.compressor == 0);              // held, not unknown
    r.fin(8.0f);
    r.run(300);
    CHECK(r.out.no_response);                  // still 10 min from the relay closing
}

TEST_CASE("the fin slope uses real sample times, so late ticks don't inflate it") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.box(8.0f);
    r.step_ms = 1200;                          // every tick 20 % late
    float f = 8.0f; r.fin(f);
    for (int i = 0; i < 180; i++) { f -= 0.45f * 1.2f / 60.0f; r.fin(f); r.tick(); }   // -0.45 C/min real
    CHECK(r.out.fin_slope == doctest::Approx(-0.45f).epsilon(0.05));
    CHECK(r.out.compressor == 0);              // -0.45 is under the -0.5 latch
}

TEST_CASE("a relay open for longer than 49.7 days still counts as rested") {
    Rig r; r.box(4.0f); r.fin(4.0f);
    r.tick();
    r.now += 0x80000000u; r.tick();
    r.now += 0x80000000u + 100000u;            // 2^32 ms + 100 s since it opened
    r.box(10.0f); r.tick();
    CHECK(r.out.relay);                        // not held by a wrapped 100 s < minofftime
}

TEST_CASE("no_response clears as soon as the compressor starts") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.box(8.0f); r.fin(8.0f);
    r.tick(); r.run(600);
    REQUIRE(r.out.no_response);
    float f = 8.0f;
    for (int i = 0; i < 70; i++) { f -= 1.0f / 60.0f; r.fin(f); r.tick(); }
    CHECK(r.out.compressor == 1);
    CHECK_FALSE(r.out.no_response);
}

TEST_CASE("no_response is not evaluated while the fin sensor is faulted") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.box(8.0f); r.fin_open();
    r.run(15 * 60);
    REQUIRE(r.out.fin_fault);
    CHECK(r.out.relay);
    CHECK_FALSE(r.out.no_response);
}

TEST_CASE("calibration takes no points before settle or while cooling") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.s.settle = 10; r.s.coolerset = 30;
    r.box(10.0f); r.fin(10.0f); r.tick();
    cal_start(r.st);
    r.run(9 * 60);
    CHECK(r.out.cal_points == 0);              // relay open, but not for settle yet
    r.run(4 * 60);
    CHECK(r.out.cal_points == 1);
    r.s.coolerset = 2;                         // box now warm: the relay closes
    r.box(20.0f); r.fin(20.0f);
    r.run(30 * 60);
    CHECK(r.out.relay);
    CHECK(r.out.cal_points == 1);              // nothing taken while cooling
}
