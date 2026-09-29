#include "doctest/doctest.h"
#include "harness.h"
#include "plant.h"

using namespace cooler;

namespace {

// Runs controller + plant together, checking the invariants every tick.
struct Sim {
    th::Rig r;
    th::Plant p;
    bool sht_dead = false, fin_dead = false;
    uint32_t prev_change = 0;
    bool prev_relay = false;
    int ticks_closed_below_cutoff = 0;   // consecutive
    int max_closed_below_cutoff = 0;
    bool minoff_violated = false, minrun_violated = false;
    uint32_t relay_on_ticks = 0, ticks = 0;

    Sim() { r.s.coolerset = 4; r.s.range = 2; }

    void tick() {
        if (sht_dead) r.box_dead(); else r.box(p.box);
        if (fin_dead) r.fin_open(); else r.fin(p.fin);
        uint32_t now = r.now;
        const Outputs& o = r.tick();
        if (o.relay != prev_relay) {
            uint32_t held = now - prev_change;
            if (o.relay && held < (uint32_t)r.s.minofftime * 60000u) minoff_violated = true;
            if (!o.relay && !o.defrost && held < (uint32_t)r.s.minruntime * 1000u) minrun_violated = true;
            prev_relay = o.relay; prev_change = now;
        }
        if (o.relay && !fin_dead && p.fin <= (float)r.s.fin_cutoff) ticks_closed_below_cutoff++;
        else ticks_closed_below_cutoff = 0;
        if (ticks_closed_below_cutoff > max_closed_below_cutoff) max_closed_below_cutoff = ticks_closed_below_cutoff;
        if (o.relay) relay_on_ticks++;
        ticks++;
        p.tick(o.relay, now);
    }
    void run_h(float hours) { for (uint32_t i = 0; i < (uint32_t)(hours * 3600); i++) tick(); }
    // run, recording the box min/max over the period
    void run_h_track(float hours, float& lo, float& hi) {
        lo = 1e9f; hi = -1e9f;
        for (uint32_t i = 0; i < (uint32_t)(hours * 3600); i++) {
            tick();
            if (p.box < lo) lo = p.box;
            if (p.box > hi) hi = p.box;
        }
    }
};

}  // namespace

TEST_CASE("scenario: normal pull-down 20 -> 4 C converges and holds the band") {
    Sim s;
    float lo, hi;
    s.run_h(16);
    s.run_h_track(8, lo, hi);
    CHECK(hi <= 7.0f);
    CHECK(lo >= 1.0f);
    CHECK_FALSE(s.minoff_violated);
    CHECK_FALSE(s.minrun_violated);
    CHECK(s.max_closed_below_cutoff <= 1);
}

TEST_CASE("scenario: pull-down runs continuously until the fin first reaches cutoff") {
    Sim s;
    bool opened_before_cutoff = false, closed_once = false;
    for (int i = 0; i < 16 * 3600; i++) {
        s.tick();
        if (s.r.out.relay) closed_once = true;
        if (closed_once && !s.r.out.relay) {
            if (!s.r.out.defrost) opened_before_cutoff = true;
            break;
        }
    }
    CHECK(closed_once);
    CHECK_FALSE(opened_before_cutoff);
}

TEST_CASE("scenario: defrost never latches forever with the box below fin_recover") {
    Sim s; s.r.s.fin_recover = 10; s.r.s.coolerset = 2; s.r.s.range = 0;
    s.run_h(24);
    // box sits near 2 C, far below fin_recover 10: defrost must still cycle out
    int defrost_ticks = 0;
    for (int i = 0; i < 3600; i++) { s.tick(); if (s.r.out.defrost) defrost_ticks++; }
    CHECK(defrost_ticks < 3600);
    CHECK(s.max_closed_below_cutoff <= 1);
}

TEST_CASE("scenario: override switch 24 h holds 3..5 C plus AC lag, defrost still fires") {
    Sim s; s.r.in.switch_closed = true; s.r.s.coolerset = 20;   // ignored in override
    float lo, hi;
    s.run_h(16);
    bool saw_defrost = false;
    lo = 1e9f; hi = -1e9f;
    for (int i = 0; i < 8 * 3600; i++) {
        s.tick();
        if (s.r.out.defrost) saw_defrost = true;
        if (s.p.box < lo) lo = s.p.box;
        if (s.p.box > hi) hi = s.p.box;
    }
    CHECK(s.r.out.mode == Mode::Override);
    CHECK(hi <= 6.0f);
    CHECK(lo >= 2.0f);
    CHECK(saw_defrost);
    CHECK(s.max_closed_below_cutoff <= 1);
}

TEST_CASE("scenario: link lost 30 min then restored") {
    Sim s; s.run_h(12);
    s.r.in.mqtt_connected = false;
    for (int i = 0; i < 61; i++) s.tick();
    CHECK(s.r.out.mode == Mode::Override);
    for (int i = 0; i < 30 * 60; i++) s.tick();
    s.r.in.mqtt_connected = true;
    for (int i = 0; i < 59; i++) s.tick();
    CHECK(s.r.out.mode == Mode::Override);
    for (int i = 0; i < 2; i++) s.tick();
    CHECK(s.r.out.mode == Mode::Normal);
}

TEST_CASE("scenario: SHT30 dies mid-run -> fin-proxy holds within +-3 C of the band") {
    Sim s; s.run_h(16);
    s.sht_dead = true;
    for (int i = 0; i < 301; i++) s.tick();
    CHECK(s.r.out.mode == Mode::FinProxy);
    float lo, hi;
    s.run_h_track(12, lo, hi);
    CHECK(hi <= 9.0f);
    CHECK(lo >= -1.0f);
    CHECK(s.max_closed_below_cutoff <= 1);
}

TEST_CASE("scenario: both sensors fail -> blind duty matches dutypercent") {
    Sim s; s.r.s.minruntime = 0; s.r.s.minofftime = 0;
    s.sht_dead = true; s.fin_dead = true;
    for (int i = 0; i < 400; i++) s.tick();
    REQUIRE(s.r.out.mode == Mode::Blind);
    s.relay_on_ticks = 0; s.ticks = 0;
    s.run_h(4);
    double duty = 100.0 * s.relay_on_ticks / s.ticks;
    CHECK(duty == doctest::Approx(50.0).epsilon(0.02));
}

TEST_CASE("scenario: AC ignores the relay -> no_response at 10 min") {
    Sim s; s.p.unplugged = true;
    // relay closes after minofftime (5 min) from boot, then 10 min more
    for (int i = 0; i < 5 * 60 + 2; i++) s.tick();
    REQUIRE(s.r.out.relay);
    for (int i = 0; i < 9 * 60; i++) s.tick();
    CHECK_FALSE(s.r.out.no_response);
    for (int i = 0; i < 61; i++) s.tick();
    CHECK(s.r.out.no_response);
}

TEST_CASE("scenario: compressor inference tracks the real compressor within 90 s") {
    Sim s; s.p.min_on_ms = 300000; s.r.s.minruntime = 180;
    uint32_t mismatch_run = 0, worst = 0;
    for (int i = 0; i < 16 * 3600; i++) {
        s.tick();
        int inferred = s.r.out.compressor;
        if (inferred >= 0 && (inferred == 1) != s.p.comp) mismatch_run++;
        else mismatch_run = 0;
        if (mismatch_run > worst) worst = mismatch_run;
    }
    CHECK(worst <= 90);
}

TEST_CASE("scenario: boot -> relay open for minofftime, override follows the switch") {
    Sim s; s.r.in.switch_closed = true;
    for (int i = 0; i < 5 * 60 - 1; i++) { s.tick(); REQUIRE_FALSE(s.r.out.relay); }
    CHECK(s.r.out.mode == Mode::Override);
    CHECK(s.r.out.override_src == OverrideSrc::Switch);
}
