#include <doctest/doctest.h>
#include "bounds.h"
#include <cstring>

TEST_CASE("every bound is well formed") {
    REQUIRE(BOUNDS_N == 10);
    for (size_t i = 0; i < BOUNDS_N; i++) {
        CHECK(BOUNDS[i].key != nullptr);
        CHECK(BOUNDS[i].label != nullptr);
        CHECK(BOUNDS[i].lo <= BOUNDS[i].hi);
    }
}

TEST_CASE("find locates keys and rejects unknowns and retired v3 keys") {
    REQUIRE(bounds_find("coolerset") != nullptr);
    CHECK(bounds_find("coolerset")->hi == 40);
    CHECK(bounds_find("nonsense") == nullptr);
    CHECK(bounds_find("screentimeout") == nullptr);
    CHECK(bounds_find("override") == nullptr);
}

TEST_CASE("clamp holds each value inside the controller's range") {
    CHECK(bounds_clamp("coolerset", 1, 0) == 2);
    CHECK(bounds_clamp("coolerset", 41, 0) == 40);
    CHECK(bounds_clamp("range", -1, 0) == 0);
    CHECK(bounds_clamp("range", 6, 0) == 5);
    CHECK(bounds_clamp("fin_cutoff", -6, 0) == -5);
    CHECK(bounds_clamp("fin_cutoff", 6, 0) == 5);
    CHECK(bounds_clamp("settle", 1, 0) == 2);
    CHECK(bounds_clamp("settle", 31, 0) == 30);
    CHECK(bounds_clamp("minofftime", 31, 0) == 30);
    CHECK(bounds_clamp("minruntime", -5, 0) == 0);
    CHECK(bounds_clamp("minruntime", 601, 0) == 600);
    CHECK(bounds_clamp("maxrun", 0, 0) == 1);
    CHECK(bounds_clamp("maxrun", 61, 0) == 60);
    CHECK(bounds_clamp("dutypercent", 1, 0) == 5);
    CHECK(bounds_clamp("dutypercent", 101, 0) == 100);
    CHECK(bounds_clamp("sampleinterval", 5, 0) == 10);
}

TEST_CASE("maxrun no longer depends on minofftime") {
    CHECK(bounds_clamp("maxrun", 3, 30) == 3);
}

TEST_CASE("fin_recover floor tracks the current fin_cutoff") {
    CHECK(bounds_clamp("fin_recover", 0, 0) == 1);
    CHECK(bounds_clamp("fin_recover", 3, 0) == 3);
    CHECK(bounds_clamp("fin_recover", 3, 4) == 5);
    CHECK(bounds_clamp("fin_recover", -4, -5) == -4);
    CHECK(bounds_clamp("fin_recover", 11, 0) == 10);
}

TEST_CASE("unknown keys pass through unchanged") {
    CHECK(bounds_clamp("nonsense", 12345, 0) == 12345);
}
