#include <doctest/doctest.h>
#include "panel_config.h"

TEST_CASE("defaults match the design spec") {
    PanelConfig c;
    CHECK(c.alarm.silent_s == 300);
    CHECK(c.alarm.over_c == 8);      // calibrated to this cooler, see alarm.h
    CHECK(c.alarm.over_s == 21600);  // 6 h sustained
    CHECK(c.alarm.holdoff_s == 1800);
    CHECK(c.backlight == 80);
    CHECK(c.night_dim == true);
    CHECK(c.default_zoom_s == 3600);
    CHECK(c.hum_low == 50);
    CHECK(c.hum_high == 80);
}

TEST_CASE("json round trip preserves every field") {
    PanelConfig c;
    c.alarm.silent_s = 120; c.alarm.over_c = 3;
    c.alarm.over_s = 900;   c.alarm.holdoff_s = 600;
    c.backlight = 40; c.night_dim = false; c.default_zoom_s = 21600; c.hum_low = 70; c.hum_high = 95;

    PanelConfig r;
    REQUIRE(panel_config_from_json(panel_config_to_json(c).c_str(), r));
    CHECK(r.alarm.silent_s == 120);
    CHECK(r.alarm.over_c == 3);
    CHECK(r.alarm.over_s == 900);
    CHECK(r.alarm.holdoff_s == 600);
    CHECK(r.backlight == 40);
    CHECK(r.night_dim == false);
    CHECK(r.default_zoom_s == 21600);
    CHECK(r.hum_low == 70);
    CHECK(r.hum_high == 95);
}

TEST_CASE("absent keys fall back to defaults") {
    PanelConfig r;
    REQUIRE(panel_config_from_json("{\"backlight\":25}", r));
    CHECK(r.backlight == 25);
    CHECK(r.alarm.silent_s == 300);      // default preserved
    CHECK(r.default_zoom_s == 3600);
}

TEST_CASE("malformed json is rejected") {
    PanelConfig r;
    CHECK_FALSE(panel_config_from_json("{nope", r));
}

TEST_CASE("out-of-range values are clamped on load") {
    PanelConfig r;
    REQUIRE(panel_config_from_json(
        "{\"backlight\":999,\"alarm\":{\"holdoff_s\":-5}}", r));
    CHECK(r.backlight == 100);
    CHECK(r.alarm.holdoff_s == 0);
}

TEST_CASE("an inverted humidity band is rejected, not painted both colours") {
    PanelConfig r;
    REQUIRE(panel_config_from_json("{\"hum_low\":95,\"hum_high\":70}", r));
    CHECK(r.hum_low == 50);      // snapped back to the sane default pair
    CHECK(r.hum_high == 80);
    CHECK(r.hum_low < r.hum_high);
}

TEST_CASE("humidity thresholds clamp to 0..100") {
    PanelConfig r;
    REQUIRE(panel_config_from_json("{\"hum_low\":-5,\"hum_high\":150}", r));
    CHECK(r.hum_low == 0);
    CHECK(r.hum_high == 100);
}

TEST_CASE("the trend zooms are 1 h, 3 h and 6 h; a retired 24 h or 7 d falls back to 1 h") {
    PanelConfig r;
    REQUIRE(panel_config_from_json("{\"default_zoom_s\":10800}", r));
    CHECK(r.default_zoom_s == 10800);
    REQUIRE(panel_config_from_json("{\"default_zoom_s\":21600}", r));
    CHECK(r.default_zoom_s == 21600);
    REQUIRE(panel_config_from_json("{\"default_zoom_s\":86400}", r));
    CHECK(r.default_zoom_s == 3600);
    REQUIRE(panel_config_from_json("{\"default_zoom_s\":604800}", r));
    CHECK(r.default_zoom_s == 3600);
}

