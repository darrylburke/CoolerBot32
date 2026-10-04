#include <doctest/doctest.h>
#include "backlight_policy.h"

static const uint32_t MIN = 60u * 1000u;

TEST_CASE("daytime: full brightness until 30 min without a touch, then dim") {
    CHECK(backlight_level(255, false, 0, false) == 255);
    CHECK(backlight_level(255, false, 29 * MIN, false) == 255);
    CHECK(backlight_level(255, false, 30 * MIN, false) == kBacklightDim);
    CHECK(backlight_level(255, false, 300 * MIN, false) == kBacklightDim);
}

TEST_CASE("night: dim 30 s after a touch, as before") {
    CHECK(backlight_level(255, true, 10 * 1000, false) == 255);
    CHECK(backlight_level(255, true, 30 * 1000, false) == kBacklightDim);
}

TEST_CASE("an alarm keeps the screen bright, day or night") {
    CHECK(backlight_level(255, false, 300 * MIN, true) == 255);
    CHECK(backlight_level(255, true, 300 * MIN, true) == 255);
}

TEST_CASE("dimming never raises a level already below the dim level") {
    CHECK(backlight_level(30, false, 300 * MIN, false) == 30);
    CHECK(backlight_level(30, false, 0, false) == 30);
}
