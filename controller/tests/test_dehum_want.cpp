#include "doctest/doctest.h"
#include "cooler_logic.h"

using namespace cooler;

TEST_CASE("dehum_want truth table") {
    DehumAuto a;
    CHECK(dehum_want(a, false, true, 70.0f, 65));
    DehumAuto b;
    CHECK_FALSE(dehum_want(b, true, true, 70.0f, 65));
    DehumAuto c;
    CHECK_FALSE(dehum_want(c, false, false, 70.0f, 65));
}

TEST_CASE("rh hysteresis") {
    DehumAuto a;
    CHECK_FALSE(dehum_want(a, false, true, 64.9f, 65));
    CHECK(dehum_want(a, false, true, 65.0f, 65));
    CHECK(dehum_want(a, false, true, 62.1f, 65));
    CHECK_FALSE(dehum_want(a, false, true, 61.9f, 65));
    CHECK_FALSE(dehum_want(a, false, true, 64.0f, 65));
}

TEST_CASE("ac cooling blocks want but keeps the humid latch") {
    DehumAuto a;
    CHECK(dehum_want(a, false, true, 70.0f, 65));
    CHECK_FALSE(dehum_want(a, true, true, 63.0f, 65));
    CHECK(dehum_want(a, false, true, 63.0f, 65));
}

TEST_CASE("invalid rh resets latch") {
    DehumAuto a;
    CHECK(dehum_want(a, false, true, 70.0f, 65));
    CHECK_FALSE(dehum_want(a, false, false, NAN, 65));
    CHECK_FALSE(dehum_want(a, false, true, 63.0f, 65));
}

TEST_CASE("clamp_dehum_rh") {
    CHECK(clamp_dehum_rh(29) == 30);
    CHECK(clamp_dehum_rh(30) == 30);
    CHECK(clamp_dehum_rh(95) == 95);
    CHECK(clamp_dehum_rh(96) == 95);
    CHECK(DehumCfg{}.dehum_rh == 65);
}
