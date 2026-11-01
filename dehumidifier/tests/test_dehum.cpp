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
        in.rh = 50.0f;        // dry and mild: no default rule matches
        in.temp = 5.0f;
        in.rh_age_ms = 0;
        in.temp_age_ms = 0;
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

// A settings record with every rule disabled except the ones a test sets.
Settings no_rules(int min_run = 0) {
    Settings s; s.min_run = min_run;
    for (Rule& r : s.rules) r.en = 0;
    return s;
}

}  // namespace

TEST_CASE("clamp_settings bounds") {
    struct B { int Settings::*f; int lo, hi; };
    const B bounds[] = {
        {&Settings::min_run, 0, 60}, {&Settings::timer_min, 1, 1440}, {&Settings::cycle_off, 1, 240},
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
    Settings s; s.mode = Mode::Timer; s.timer_min = 60;
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
    r.cmd(mode_cmd(Mode::Timer));                    // timer_min 120
    r.run(30 * 60 * 1000);
    CHECK(r.o.remain_s == 5400);
    Cmd t = mode_cmd(Mode::Timer); t.timer_min = 180;
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
    Settings s; s.timer_min = 60;
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

TEST_CASE("timers survive millis wrap and late ticks") {
    Settings s; s.timer_min = 60;
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

TEST_CASE("timer takes whole minutes, default 120") {
    CHECK(Settings{}.timer_min == 120);
    Settings s; s.timer_min = 45;
    Rig r(s);
    r.cmd(mode_cmd(Mode::Timer));
    CHECK(r.o.remain_s == 2700);
    r.run(2699 * 1000);
    CHECK(r.o.relay);
    r.run(1000);
    CHECK_FALSE(r.o.relay);
    CHECK(r.o.mode == Mode::Auto);
}

TEST_CASE("rule defaults") {
    const Settings s;
    const Rule r1{1, 1, 1, 65, 1, 1}, r2{0, 1, 0, 55, 1, 0}, r3{0, 0, 1, 10, 2, 1}, r4{0, 0, 0, 2, 2, 1};
    CHECK(s.rules[0] == r1);
    CHECK(s.rules[1] == r2);
    CHECK(s.rules[2] == r3);
    CHECK(s.rules[3] == r4);
    CHECK(s.mode == Mode::Auto);
    Settings c = s; clamp_settings(c);
    for (int i = 0; i < NUM_RULES; i++) CHECK(c.rules[i] == s.rules[i]);
}

TEST_CASE("rule clamping") {
    struct B { int Rule::*f; int lo, hi; };
    const B bounds[] = {
        {&Rule::en, 0, 1}, {&Rule::sensor, 0, 1}, {&Rule::cmp, 0, 1}, {&Rule::relay, 1, 2}, {&Rule::action, 0, 1},
    };
    for (auto b : bounds) {
        Rule r; r.*b.f = b.lo - 1; clamp_rule(r); CHECK(r.*b.f == b.lo);
        r = Rule{}; r.*b.f = b.hi + 1; clamp_rule(r); CHECK(r.*b.f == b.hi);
        r = Rule{}; r.*b.f = b.hi; clamp_rule(r); CHECK(r.*b.f == b.hi);
    }
    Rule t; t.sensor = 0;
    t.value = -21; clamp_rule(t); CHECK(t.value == -20);
    t.value = 51; clamp_rule(t); CHECK(t.value == 50);
    t.value = -20; clamp_rule(t); CHECK(t.value == -20);
    Rule h; h.sensor = 1;
    h.value = -1; clamp_rule(h); CHECK(h.value == 0);
    h.value = 101; clamp_rule(h); CHECK(h.value == 100);
    h.value = 100; clamp_rule(h); CHECK(h.value == 100);

    // Changing the sensor re-clamps the value to the new sensor's range.
    Settings s; State st;
    Cmd c; c.rules[0].sensor = 0;                      // r1 was humidity 65
    CHECK(apply_cmd(c, s, st));
    CHECK(s.rules[0].sensor == 0);
    CHECK(s.rules[0].value == 50);
    Cmd d; d.rules[3].value = -20; d.rules[3].sensor = 1; // value and sensor in one message
    CHECK(apply_cmd(d, s, st));
    CHECK(s.rules[3].value == 0);
}

TEST_CASE("humidity above: 3 %RH band") {
    Settings s = no_rules();
    s.rules[0] = {1, 1, 1, 65, 1, 1};
    Rig r(s);
    auto at = [&](float rh) { r.in.rh = rh; r.tick(); return r.o.match[0]; };
    CHECK_FALSE(at(64.9f));
    CHECK(at(65.0f));
    CHECK(r.o.relay);
    CHECK(at(62.1f));
    CHECK(at(62.0f));
    CHECK_FALSE(at(61.9f));
    CHECK_FALSE(r.o.relay);
    CHECK_FALSE(at(64.0f));
}

TEST_CASE("humidity below: 3 %RH band") {
    Settings s = no_rules();
    s.rules[1] = {1, 1, 0, 40, 1, 1};
    Rig r(s);
    auto at = [&](float rh) { r.in.rh = rh; r.tick(); return r.o.match[1]; };
    CHECK_FALSE(at(40.1f));
    CHECK(at(40.0f));
    CHECK(at(43.0f));
    CHECK_FALSE(at(43.1f));
    CHECK_FALSE(at(41.0f));
}

TEST_CASE("temperature above: 1 degC band") {
    Settings s = no_rules();
    s.rules[2] = {1, 0, 1, 10, 2, 1};
    Rig r(s);
    auto at = [&](float t) { r.in.temp = t; r.tick(); return r.o.match[2]; };
    CHECK_FALSE(at(9.9f));
    CHECK(at(10.0f));
    CHECK(r.o.relay2);
    CHECK_FALSE(r.o.relay);
    CHECK(at(9.0f));
    CHECK_FALSE(at(8.9f));
    CHECK_FALSE(r.o.relay2);
    CHECK_FALSE(at(9.5f));
}

TEST_CASE("temperature below: 1 degC band, negative values") {
    Settings s = no_rules();
    s.rules[3] = {1, 0, 0, -5, 2, 1};
    Rig r(s);
    auto at = [&](float t) { r.in.temp = t; r.tick(); return r.o.match[3]; };
    CHECK_FALSE(at(-4.9f));
    CHECK(at(-5.0f));
    CHECK(at(-4.0f));
    CHECK_FALSE(at(-3.9f));
    CHECK_FALSE(at(-4.5f));
}

TEST_CASE("stale or NaN reading: no match, latch cleared") {
    Settings s = no_rules();
    s.rules[0] = {1, 1, 1, 65, 1, 1};
    s.rules[2] = {1, 0, 1, 10, 2, 1};
    Rig r(s);
    r.in.rh = 70.0f; r.in.temp = 12.0f;
    r.tick();
    REQUIRE(r.o.match[0]);
    REQUIRE(r.o.match[2]);
    r.in.rh_age_ms = 29999; r.in.temp_age_ms = 29999;
    CHECK(r.tick().match[0]);
    r.in.rh_age_ms = 30000; r.in.temp_age_ms = 30000;  // both readings stale
    r.tick();
    CHECK_FALSE(r.o.match[0]);
    CHECK_FALSE(r.o.match[2]);
    CHECK_FALSE(r.o.rh_valid);
    CHECK_FALSE(r.o.temp_valid);
    CHECK_FALSE(r.o.relay);
    CHECK_FALSE(r.o.relay2);
    r.in.rh_age_ms = 0; r.in.temp_age_ms = 0;
    r.in.rh = 63.0f;                                   // inside the band, but the latch was cleared
    r.in.temp = 9.5f;
    r.tick();
    CHECK_FALSE(r.o.match[0]);
    CHECK_FALSE(r.o.match[2]);
    r.in.rh = 70.0f; r.in.temp = 12.0f;
    r.tick();
    REQUIRE(r.o.match[0]);
    r.in.rh = NAN;                                     // humidity NaN, temperature fine
    r.tick();
    CHECK_FALSE(r.o.match[0]);
    CHECK(r.o.match[2]);
    CHECK_FALSE(r.o.rh_valid);
    CHECK(r.o.temp_valid);
}

TEST_CASE("disabled rule never matches") {
    Settings s = no_rules();
    s.rules[0] = {0, 1, 1, 65, 1, 1};
    Rig r(s);
    r.in.rh = 90.0f;
    r.tick();
    CHECK_FALSE(r.o.match[0]);
    CHECK_FALSE(r.o.relay);
    Cmd c; c.rules[0].en = 1;
    CHECK(r.cmd(c).match[0]);
    CHECK(r.o.relay);
    Cmd d; d.rules[0].en = 0;
    CHECK_FALSE(r.cmd(d).match[0]);
    CHECK_FALSE(r.o.relay);
}

TEST_CASE("off wins") {
    Settings s = no_rules();
    s.rules[0] = {1, 1, 1, 60, 1, 1};                 // humid -> relay 1 on
    s.rules[1] = {1, 0, 0, 2, 1, 0};                  // cold -> relay 1 off
    Rig r(s);
    r.in.rh = 70.0f; r.in.temp = 5.0f;
    CHECK(r.tick().relay);
    r.in.temp = 1.0f;                                  // both match
    r.tick();
    CHECK(r.o.match[0]);
    CHECK(r.o.match[1]);
    CHECK_FALSE(r.o.relay);
    // An off rule alone with no on rule: still off.
    r.in.rh = 50.0f;
    CHECK_FALSE(r.tick().relay);
    // Rules for the other relay do not interfere.
    Settings t = no_rules();
    t.rules[0] = {1, 1, 1, 60, 1, 1};
    t.rules[1] = {1, 1, 1, 60, 2, 0};
    Rig q(t);
    q.in.rh = 70.0f;
    q.tick();
    CHECK(q.o.relay);
    CHECK_FALSE(q.o.relay2);
}

TEST_CASE("relay 2 only in AUTO") {
    Settings s = no_rules();
    s.rules[2] = {1, 0, 1, 10, 2, 1};
    Rig r(s);
    r.in.temp = 15.0f;
    REQUIRE(r.tick().relay2);
    CHECK_FALSE(r.o.relay);
    for (Mode m : {Mode::On, Mode::Timer, Mode::Cycle}) {
        r.cmd(mode_cmd(Mode::Auto));
        REQUIRE(r.o.relay2);
        r.cmd(mode_cmd(m));
        CHECK(r.o.relay);
        CHECK_FALSE(r.o.relay2);
        CHECK(r.o.match[2]);                           // still judged, just not acted on
    }
    // ON / TIMER / CYCLE ignore the rules for relay 1 too.
    Settings t = no_rules();
    t.rules[1] = {1, 1, 1, 0, 1, 0};                  // humidity >= 0 -> relay 1 off, always
    Rig q(t);
    CHECK(q.cmd(mode_cmd(Mode::On)).relay);
    q.in.rh = NAN;
    CHECK(q.tick().relay);
}

TEST_CASE("OFF turns both off at once") {
    Settings s = no_rules(10);
    s.rules[0] = {1, 1, 1, 60, 1, 1};
    s.rules[2] = {1, 0, 1, 10, 2, 1};
    Rig r(s);
    r.in.rh = 70.0f; r.in.temp = 15.0f;
    r.tick();
    REQUIRE(r.o.relay);
    REQUIRE(r.o.relay2);
    CHECK(r.o.hold_s > 0);
    CHECK(r.o.hold2_s > 0);
    r.cmd(mode_cmd(Mode::Off));
    CHECK_FALSE(r.o.relay);
    CHECK_FALSE(r.o.relay2);
    CHECK(r.o.hold_s == 0);
    CHECK(r.o.hold2_s == 0);
}

TEST_CASE("min_run holds each relay on its own") {
    Settings s = no_rules(10);
    s.rules[0] = {1, 1, 1, 60, 1, 1};
    s.rules[2] = {1, 0, 1, 10, 2, 1};
    Rig r(s);
    r.in.rh = 70.0f;
    r.tick();                                          // relay 1 closes at t = 0
    REQUIRE(r.o.relay);
    CHECK(r.o.hold_s == 600);
    CHECK_FALSE(r.o.relay2);
    CHECK(r.o.hold2_s == 0);
    r.run(120 * 1000);
    r.in.temp = 15.0f;
    r.tick();                                          // relay 2 closes at t = 121 s
    REQUIRE(r.o.relay2);
    CHECK(r.o.hold2_s == 600);
    CHECK(r.o.hold_s == 479);
    r.in.rh = 50.0f; r.in.temp = 5.0f;                 // neither wanted any more
    r.run(478 * 1000);                                 // t = 599
    CHECK(r.o.relay);
    CHECK(r.o.hold_s == 1);
    r.tick();                                          // t = 600: relay 1 released
    CHECK_FALSE(r.o.relay);
    CHECK(r.o.relay2);
    CHECK(r.o.hold2_s == 121);
    r.run(120 * 1000);
    CHECK(r.o.relay2);
    r.tick();                                          // t = 721: relay 2 released
    CHECK_FALSE(r.o.relay2);
    CHECK(r.o.hold2_s == 0);
}

TEST_CASE("min_run holds relay 2 when leaving AUTO") {
    Settings s = no_rules(10);
    s.rules[2] = {1, 0, 1, 10, 2, 1};
    Rig r(s);
    r.in.temp = 15.0f;
    REQUIRE(r.tick().relay2);
    r.run(60 * 1000);
    r.cmd(mode_cmd(Mode::On));
    CHECK(r.o.relay);
    CHECK(r.o.relay2);                                 // held: 9 min left
    CHECK(r.o.hold2_s == 540);
    r.run(540 * 1000);
    CHECK_FALSE(r.o.relay2);
}

TEST_CASE("broker loss turns both off") {
    Settings s = no_rules(10);
    s.rules[0] = {1, 1, 1, 60, 1, 1};
    s.rules[2] = {1, 0, 1, 10, 2, 1};
    Rig r(s);
    r.in.rh = 70.0f; r.in.temp = 15.0f;
    r.tick();
    REQUIRE(r.o.relay);
    REQUIRE(r.o.relay2);
    r.in.mqtt_connected = false;
    r.run(59 * 1000);
    CHECK(r.o.relay);
    CHECK(r.o.relay2);
    r.tick();
    CHECK(r.o.link_lost);
    CHECK_FALSE(r.o.relay);
    CHECK_FALSE(r.o.relay2);
    r.in.mqtt_connected = true;
    r.tick();
    CHECK(r.o.relay);
    CHECK(r.o.relay2);
}

TEST_CASE("never connected: both off") {
    Settings s = no_rules();
    s.rules[0] = {1, 1, 1, 60, 1, 1};
    s.rules[2] = {1, 0, 1, 10, 2, 1};
    Rig r(s, false);
    r.in.rh = 70.0f; r.in.temp = 15.0f;
    r.run(5 * 60 * 1000);
    CHECK(r.o.match[0]);
    CHECK(r.o.match[2]);
    CHECK_FALSE(r.o.relay);
    CHECK_FALSE(r.o.relay2);
    r.in.mqtt_connected = true;
    r.tick();
    CHECK(r.o.relay);
    CHECK(r.o.relay2);
}

TEST_CASE("a changed rule loses its latch") {
    Settings s = no_rules();
    s.rules[0] = {1, 1, 1, 65, 1, 1};
    Rig r(s);
    r.in.rh = 66.0f;
    REQUIRE(r.tick().match[0]);
    r.in.rh = 63.0f;                                   // in the band: still latched
    REQUIRE(r.tick().match[0]);
    Cmd c; c.rules[0].relay = 2;
    CHECK(r.cmd(c).relay2 == false);                   // re-judged from 65: 63 is not above
    CHECK_FALSE(r.o.match[0]);
    // Re-sending the same values is no change and keeps the latch.
    r.in.rh = 66.0f;
    REQUIRE(r.tick().match[0]);
    r.in.rh = 63.0f;
    REQUIRE(r.tick().match[0]);
    Cmd same; same.rules[0].relay = 2; same.rules[0].value = 65;
    State st2 = r.st; Settings s2 = r.s;
    CHECK_FALSE(apply_cmd(same, s2, st2));
    CHECK(st2.match[0]);
    // A change to another rule leaves this latch alone.
    Cmd other; other.rules[1].value = 50;
    CHECK(apply_cmd(other, s2, st2));
    CHECK(st2.match[0]);
}

TEST_CASE("apply_cmd: every rN key, returns changed") {
    Settings s; State st;
    CHECK_FALSE(apply_cmd(Cmd{}, s, st));
    for (int i = 0; i < NUM_RULES; i++) {
        Settings t; State u;
        Rule want = t.rules[i];
        Cmd c;
        auto flip = [&](std::optional<int> RuleCmd::*cf, int Rule::*rf, int v) {
            Cmd one; (one.rules[i].*cf) = v;
            Settings a; State b;
            const bool changed = apply_cmd(one, a, b);
            CHECK(changed);
            CHECK(a.rules[i].*rf == v);
            (c.rules[i].*cf) = v;
            want.*rf = v;
        };
        flip(&RuleCmd::en, &Rule::en, 1 - t.rules[i].en);
        flip(&RuleCmd::cmp, &Rule::cmp, 1 - t.rules[i].cmp);
        flip(&RuleCmd::value, &Rule::value, 7);
        flip(&RuleCmd::relay, &Rule::relay, 3 - t.rules[i].relay);
        flip(&RuleCmd::action, &Rule::action, 1 - t.rules[i].action);
        flip(&RuleCmd::sensor, &Rule::sensor, 1 - t.rules[i].sensor);
        CHECK(apply_cmd(c, t, u));
        CHECK(t.rules[i] == want);
        for (int j = 0; j < NUM_RULES; j++)
            if (j != i) CHECK(t.rules[j] == Settings{}.rules[j]);
        CHECK_FALSE(apply_cmd(c, t, u));               // same again: no change
    }
    // Out of range is clamped; a clamp back to the stored value is no change.
    Settings a; State b;
    Cmd c; c.rules[0].relay = 9;
    CHECK(apply_cmd(c, a, b));
    CHECK(a.rules[0].relay == 2);
    CHECK_FALSE(apply_cmd(c, a, b));
}

TEST_CASE("publish_now on relay 2 and match changes") {
    Settings s = no_rules();
    s.rules[2] = {1, 0, 1, 10, 2, 1};
    s.rules[3] = {1, 0, 0, 2, 1, 0};                  // relay 1 off: match only
    Rig r(s);
    CHECK_FALSE(r.tick().publish_now);
    r.in.temp = 15.0f;
    CHECK(r.tick().publish_now);                       // relay 2 on, r3 met
    CHECK_FALSE(r.tick().publish_now);
    r.in.temp = 1.0f;
    CHECK(r.tick().publish_now);
    CHECK(r.o.match[3]);
    CHECK_FALSE(r.o.relay);
    CHECK_FALSE(r.tick().publish_now);
    r.in.temp = 1.5f;                                  // reading moved, nothing published changed
    CHECK_FALSE(r.tick().publish_now);
    r.in.temp = NAN;
    CHECK(r.tick().publish_now);                       // temp went null, r4 unmet
}

TEST_CASE("humidity and temperature go stale separately") {
    // The HDC1080 driver publishes temperature, then gives up quietly if the
    // humidity read fails: a fresh temperature must not vouch for an old humidity.
    Settings s = no_rules();
    s.rules[0] = {1, 1, 1, 65, 1, 1};   // humidity above 65
    s.rules[2] = {1, 0, 1, 10, 2, 1};   // temp above 10
    Rig r(s);
    r.in.rh = 70.0f; r.in.temp = 12.0f;
    r.tick();
    REQUIRE(r.o.match[0]);
    REQUIRE(r.o.match[2]);
    r.in.rh_age_ms = 30000; r.in.temp_age_ms = 0;
    r.tick();
    CHECK_FALSE(r.o.rh_valid);
    CHECK(r.o.temp_valid);
    CHECK_FALSE(r.o.match[0]);
    CHECK(r.o.match[2]);
    r.in.rh_age_ms = 0; r.in.temp_age_ms = 30000;
    r.tick();
    CHECK(r.o.rh_valid);
    CHECK_FALSE(r.o.temp_valid);
    CHECK_FALSE(r.o.match[2]);
}
