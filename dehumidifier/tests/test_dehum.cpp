#include "doctest/doctest.h"
#include "dehum_logic.h"

#include <cmath>
#include <cstring>

using namespace dehum;

namespace {

// Steps the logic like the YAML does: one call per tick, `now` in ms.
struct Rig {
    Settings s;
    State st;
    Inputs in;
    uint32_t now;
    Outputs o;

    explicit Rig(Settings s0 = Settings{}, bool connected = true, uint32_t start = 1000)
        : s(s0), now(start) {
        in.mqtt_connected = connected;
        in.rh = 70.0f;        // humid unless a test says otherwise
        in.rh_age_ms = 0;
        o = step(in, s, st, now);
    }
    Outputs tick(uint32_t ms = 1000) { now += ms; o = step(in, s, st, now); return o; }
    Outputs run(uint32_t total_ms, uint32_t t = 1000) {
        for (uint32_t e = 0; e < total_ms; e += t) tick(t);
        return o;
    }
    // Apply a command, then step at once (the YAML runs control right after).
    Outputs cmd(const Cmd& c) { apply_cmd(c, s, st); return tick(0); }
};

Cmd mode_cmd(Mode m) { Cmd c; c.mode = m; return c; }

}  // namespace

TEST_CASE("clamp_settings bounds") {
    struct B { int Settings::*f; int lo, hi; };
    const B bounds[] = {
        {&Settings::min_run, 0, 60}, {&Settings::timer_h, 1, 24}, {&Settings::cycle_off, 1, 240},
        {&Settings::dehum_rh, 30, 95},
    };
    for (auto b : bounds) {
        Settings s; s.*b.f = b.lo - 1; clamp_settings(s); CHECK(s.*b.f == b.lo);
        s = Settings{}; s.*b.f = b.lo; clamp_settings(s); CHECK(s.*b.f == b.lo);
        s = Settings{}; s.*b.f = b.hi; clamp_settings(s); CHECK(s.*b.f == b.hi);
        s = Settings{}; s.*b.f = b.hi + 1; clamp_settings(s); CHECK(s.*b.f == b.hi);
    }
    Settings s; s.min_run = 0; s.cycle_on = 0; clamp_settings(s); CHECK(s.cycle_on == 1);
    s = Settings{}; s.cycle_on = 241; clamp_settings(s); CHECK(s.cycle_on == 240);
}

TEST_CASE("min_run bumps cycle_on") {
    Settings s; s.min_run = 40; s.cycle_on = 30; clamp_settings(s);
    CHECK(s.cycle_on == 40);

    Settings t; State st; Cmd c; c.min_run = 50; c.cycle_on = 20;
    CHECK(apply_cmd(c, t, st));
    CHECK(t.min_run == 50);
    CHECK(t.cycle_on == 50);
}

TEST_CASE("parse_mode") {
    for (Mode m : {Mode::Auto, Mode::On, Mode::Off, Mode::Timer, Mode::Cycle}) {
        auto p = parse_mode(mode_str(m));
        REQUIRE(p.has_value());
        CHECK(*p == m);
    }
    CHECK(std::strcmp(mode_str(Mode::Cycle), "cycle") == 0);
    CHECK_FALSE(parse_mode("AUTO").has_value());
    CHECK_FALSE(parse_mode("").has_value());
    CHECK_FALSE(parse_mode("x").has_value());
    CHECK_FALSE(parse_mode(nullptr).has_value());
}

TEST_CASE("boot stays off until first connect") {
    Settings s; s.mode = Mode::On;
    Rig r(s, false);
    CHECK_FALSE(r.o.relay);
    r.run(10 * 60 * 1000);
    CHECK_FALSE(r.o.relay);
    CHECK_FALSE(r.o.link_lost);
    r.in.mqtt_connected = true;
    CHECK(r.tick().relay);
}

TEST_CASE("timer restarts at first connect") {
    Settings s; s.mode = Mode::Timer; s.timer_h = 1;
    Rig r(s, false);
    r.run(5 * 60 * 1000);
    CHECK_FALSE(r.o.relay);
    r.in.mqtt_connected = true;
    r.tick();
    CHECK(r.o.relay);
    CHECK(r.o.remain_s == 3600);
    r.run(3599 * 1000);
    CHECK(r.o.relay);
    CHECK(r.o.remain_s == 1);
    CHECK(r.o.mode == Mode::Timer);
    r.tick();
    CHECK_FALSE(r.o.relay);
    CHECK(r.o.mode == Mode::Auto);
    CHECK(r.s.mode == Mode::Auto);
    CHECK(r.o.settings_changed);
    CHECK_FALSE(r.tick().settings_changed);
}

TEST_CASE("on / off") {
    Rig r;
    CHECK(r.cmd(mode_cmd(Mode::On)).relay);
    CHECK(r.o.hold_s == 600);
    r.run(60 * 1000);
    CHECK_FALSE(r.cmd(mode_cmd(Mode::Off)).relay);
    CHECK(r.o.hold_s == 0);
}

TEST_CASE("auto follows fresh want") {
    Settings s; s.min_run = 0;
    Rig r(s);
    CHECK(r.o.want == -1);
    CHECK_FALSE(r.o.relay);
    on_want(r.st, true);
    r.tick();
    CHECK(r.o.relay);
    CHECK(r.o.want == 1);
    on_want(r.st, false);
    r.tick();
    CHECK_FALSE(r.o.relay);
    CHECK(r.o.want == 0);
    on_want(r.st, true);
    r.tick();                 // want 1 s old
    r.run(88 * 1000);         // 89 s
    CHECK(r.o.relay);
    r.run(2 * 1000);          // 91 s
    CHECK_FALSE(r.o.relay);
    CHECK(r.o.want == -1);
}

TEST_CASE("min_run holds through want drop") {
    Rig r;                    // min_run 10
    on_want(r.st, true);
    r.tick();
    REQUIRE(r.o.relay);       // closed on this tick
    int k = 0;
    auto run_want = [&](bool w, int ticks) {
        for (int i = 0; i < ticks; i++) {
            if (k % 30 == 0) on_want(r.st, w);
            r.tick(); k++;
        }
    };
    run_want(true, 120);
    k = 0;
    run_want(false, 479);     // 599 s after closing
    CHECK(r.o.want == 0);
    CHECK(r.o.relay);
    CHECK(r.o.hold_s == 1);
    on_want(r.st, false);
    r.tick();                 // 600 s
    CHECK_FALSE(r.o.relay);
    CHECK(r.o.hold_s == 0);
}

TEST_CASE("cycle phases") {
    Settings s; s.cycle_on = 20; s.cycle_off = 40; s.min_run = 10;
    Rig r(s);
    r.cmd(mode_cmd(Mode::Cycle));
    CHECK(r.o.relay);
    CHECK(r.o.phase == 1);
    CHECK(r.o.remain_s == 1200);
    r.run(1199 * 1000);
    CHECK(r.o.phase == 1);
    CHECK(r.o.remain_s == 1);
    r.tick();
    CHECK(r.o.phase == 0);
    CHECK_FALSE(r.o.relay);
    CHECK(r.o.remain_s == 2400);
    r.run(2399 * 1000);
    CHECK(r.o.phase == 0);
    CHECK_FALSE(r.o.relay);
    r.tick();
    CHECK(r.o.phase == 1);
    CHECK(r.o.relay);
}

TEST_CASE("resend restarts with new values") {
    Rig r;
    r.cmd(mode_cmd(Mode::Timer));                    // timer_h 2
    r.run(30 * 60 * 1000);
    CHECK(r.o.remain_s == 5400);
    Cmd t = mode_cmd(Mode::Timer); t.timer_h = 3;
    r.cmd(t);
    CHECK(r.o.remain_s == 10800);

    r.cmd(mode_cmd(Mode::Cycle));                    // cycle_on 30
    r.run(15 * 60 * 1000);
    Cmd c = mode_cmd(Mode::Cycle); c.cycle_on = 25;
    r.cmd(c);
    CHECK(r.o.phase == 1);
    CHECK(r.o.remain_s == 1500);
}

TEST_CASE("broker loss cuts after 60 s") {
    Rig r;                                            // min_run 10
    REQUIRE(r.cmd(mode_cmd(Mode::On)).relay);
    r.in.mqtt_connected = false;
    r.run(59 * 1000);
    CHECK(r.o.relay);
    CHECK_FALSE(r.o.link_lost);
    r.tick();
    CHECK_FALSE(r.o.relay);
    CHECK(r.o.link_lost);
    CHECK(r.o.mode == Mode::On);
    r.in.mqtt_connected = true;
    r.tick();
    CHECK(r.o.relay);
    CHECK_FALSE(r.o.link_lost);
}

TEST_CASE("timer counts through broker loss") {
    Settings s; s.timer_h = 1;
    Rig r(s);
    r.cmd(mode_cmd(Mode::Timer));
    r.run(10 * 60 * 1000);
    r.in.mqtt_connected = false;
    r.run(45 * 60 * 1000);
    CHECK(r.o.link_lost);
    CHECK_FALSE(r.o.relay);
    r.in.mqtt_connected = true;
    r.tick();
    CHECK(r.o.relay);
    CHECK(r.o.remain_s == 299);
}

TEST_CASE("want remembered across modes") {
    Settings s; s.min_run = 0;
    Rig r(s);
    r.cmd(mode_cmd(Mode::On));
    on_want(r.st, true);
    r.run(30 * 1000);
    CHECK(r.cmd(mode_cmd(Mode::Auto)).relay);
    on_want(r.st, false);
    CHECK_FALSE(r.tick().relay);
}

TEST_CASE("timers survive millis wrap and late ticks") {
    Settings s; s.timer_h = 1;
    Rig r(s, true, UINT32_MAX - 30000u);
    r.cmd(mode_cmd(Mode::Timer));
    uint32_t elapsed_s = 0;
    while (r.o.relay && elapsed_s < 4000) { r.tick(5000); elapsed_s += 5; }
    CHECK(elapsed_s >= 3600);
    CHECK(elapsed_s < 3605);
    CHECK(r.o.mode == Mode::Auto);
}

TEST_CASE("publish_now on changes") {
    Rig r;
    CHECK(r.o.publish_now);                           // first tick
    CHECK_FALSE(r.tick().publish_now);
    CHECK(r.cmd(mode_cmd(Mode::On)).publish_now);
    CHECK_FALSE(r.tick().publish_now);
    CHECK(r.cmd(mode_cmd(Mode::Cycle)).publish_now);  // mode changed, relay stays on
    CHECK_FALSE(r.tick().publish_now);
}

TEST_CASE("broker loss forgets the last want") {
    Rig r;                                            // AUTO, min_run 10
    on_want(r.st, true);
    REQUIRE(r.tick().relay);
    r.run(15 * 60 * 1000);                            // past min_run (want goes stale, relay off)
    on_want(r.st, true);
    REQUIRE(r.tick().relay);
    r.run(10 * 60 * 1000);                            // hold over; keep want fresh below
    on_want(r.st, true);
    REQUIRE(r.tick().relay);
    r.in.mqtt_connected = false;
    r.run(75 * 1000);                                 // lockout at 60 s
    CHECK_FALSE(r.o.relay);
    r.in.mqtt_connected = true;
    r.tick();                                         // want is 76 s old but predates the outage
    CHECK_FALSE(r.o.relay);
    CHECK(r.o.want == -1);
    on_want(r.st, true);                              // the Cooler's next message
    CHECK(r.tick().relay);
}

TEST_CASE("dehum_rh default") { CHECK(Settings{}.dehum_rh == 65); }

TEST_CASE("auto needs local humidity") {
    Settings s; s.min_run = 0;
    Rig r(s);
    r.in.rh = 60.0f;
    on_want(r.st, true);
    r.tick();
    CHECK_FALSE(r.o.relay);
    CHECK_FALSE(r.o.humid);
    r.in.rh = 66.0f;
    r.tick();
    CHECK(r.o.relay);
    CHECK(r.o.humid);
    on_want(r.st, false);                              // AC cooling
    CHECK_FALSE(r.tick().relay);
    CHECK(r.o.humid);
}

TEST_CASE("local rh hysteresis") {
    Settings s; s.min_run = 0;
    Rig r(s);
    r.in.rh = 50.0f;                                   // start dry: the rig boots at 70 %
    r.tick();
    auto at = [&](float rh) { r.in.rh = rh; on_want(r.st, true); return r.tick().relay; };
    CHECK_FALSE(at(64.9f));
    CHECK(at(65.0f));
    CHECK(at(62.1f));
    CHECK_FALSE(at(61.9f));
    CHECK_FALSE(at(64.0f));
}

TEST_CASE("stale or missing rh blocks auto") {
    Settings s; s.min_run = 0;
    Rig r(s);
    on_want(r.st, true);
    REQUIRE(r.tick().relay);
    r.in.rh_age_ms = 29000;
    CHECK(r.tick().relay);
    r.in.rh_age_ms = 30000;
    CHECK_FALSE(r.tick().relay);
    CHECK_FALSE(r.o.humid);
    r.in.rh_age_ms = 0;
    r.in.rh = NAN;
    CHECK_FALSE(r.tick().relay);
    r.in.rh = 63.0f;                                   // latch was cleared: 63 is not humid
    CHECK_FALSE(r.tick().relay);
}

TEST_CASE("on, timer and cycle ignore rh") {
    Rig r;
    r.in.rh = NAN;
    CHECK(r.cmd(mode_cmd(Mode::On)).relay);
    CHECK(r.cmd(mode_cmd(Mode::Timer)).relay);
    CHECK(r.cmd(mode_cmd(Mode::Cycle)).relay);
}
