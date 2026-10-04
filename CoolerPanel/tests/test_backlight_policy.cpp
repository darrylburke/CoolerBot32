#include <doctest/doctest.h>
#include "backlight_policy.h"

static const uint32_t MIN = 60u * 1000u;

TEST_CASE("by day the backlight goes off after 30 min without a touch") {
    CHECK(kIdleDimMs == 30 * MIN);
}

TEST_CASE("daytime: on until the idle timeout, then the backlight goes off") {
    CHECK(backlight_level(204, false, 0, false) == 204);
    CHECK(backlight_level(204, false, kIdleDimMs - 1, false) == 204);
    CHECK(backlight_level(204, false, kIdleDimMs, false) == 0);
    CHECK(backlight_level(204, false, 300 * MIN, false) == 0);
}

TEST_CASE("night: dim to 5% 30 s after a touch, not off") {
    CHECK(kBacklightDim == 13);   // 13/255 ~ 5%: LED brightness looks far higher than its duty
    CHECK(backlight_level(204, true, 10 * 1000, false) == 204);
    CHECK(backlight_level(204, true, 30 * 1000, false) == kBacklightDim);
}

TEST_CASE("an alarm keeps the screen on, day or night") {
    CHECK(backlight_level(204, false, 300 * MIN, true) == 204);
    CHECK(backlight_level(204, true, 300 * MIN, true) == 204);
}

TEST_CASE("night dimming never raises a level already below the dim level") {
    CHECK(backlight_level(10, true, 300 * MIN, false) == 10);
    CHECK(backlight_level(10, true, 0, false) == 10);
}
