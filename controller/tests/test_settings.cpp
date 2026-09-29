#include "doctest/doctest.h"
#include "harness.h"

using namespace cooler;

TEST_CASE("clamp_settings: every bound, lo-1 / lo / hi / hi+1") {
    struct B { int Settings::*f; int lo, hi; };
    const B bounds[] = {
        {&Settings::coolerset, 2, 40}, {&Settings::range, 0, 5},
        {&Settings::settle, 2, 30}, {&Settings::minofftime, 0, 30},
        {&Settings::minruntime, 0, 600}, {&Settings::maxrun, 1, 60},
        {&Settings::dutypercent, 1, 100}, {&Settings::sampleinterval, 10, 3600},
        {&Settings::fin_cutoff, -5, 5},
    };
    for (auto b : bounds) {
        Settings s; s.*b.f = b.lo - 1; clamp_settings(s); CHECK(s.*b.f == b.lo);
        s = Settings{}; s.*b.f = b.lo; clamp_settings(s); CHECK(s.*b.f == b.lo);
        s = Settings{}; s.*b.f = b.hi; clamp_settings(s); CHECK(s.*b.f == b.hi);
        s = Settings{}; s.*b.f = b.hi + 1; clamp_settings(s); CHECK(s.*b.f == b.hi);
    }
}

TEST_CASE("clamp_settings: fin_recover floor tracks fin_cutoff") {
    Settings s; s.fin_cutoff = 4; s.fin_recover = 3; clamp_settings(s);
    CHECK(s.fin_recover == 5);
    s = Settings{}; s.fin_recover = 11; clamp_settings(s); CHECK(s.fin_recover == 10);
    s = Settings{}; s.fin_cutoff = 0; s.fin_recover = 0; clamp_settings(s); CHECK(s.fin_recover == 1);
}

TEST_CASE("fin conversion round-trips and fit_beta recovers a known curve") {
    FinCal c;
    CHECK(fin_c_from_ohms(10000.0f, c) == doctest::Approx(25.0f).epsilon(0.001));
    float v = th::volts_for_c(0.0f);
    CHECK(fin_c_from_ohms(fin_ohms_from_volts(v), c) == doctest::Approx(0.0f).epsilon(0.01));
    CHECK(std::isnan(fin_ohms_from_volts(3.3f)));

    CalPoint p[6];
    float temps[6] = {4, 8, 12, 16, 20, 24};
    for (int i = 0; i < 6; i++) {
        float r = 12000.0f * std::exp(3600.0f * (1.0f / (temps[i] + 273.15f) - 1.0f / T0_K));
        p[i] = CalPoint{r, temps[i]};
    }
    CalFit f = fit_beta(p, 6);
    CHECK(f.ok);
    CHECK(f.beta == doctest::Approx(3600.0f).epsilon(20.0 / 3600));
    CHECK(f.r0 == doctest::Approx(12000.0f).epsilon(0.02));
    CHECK(f.err_c < 0.05f);
}
