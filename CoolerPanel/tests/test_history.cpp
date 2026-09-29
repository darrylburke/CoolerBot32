#include <doctest/doctest.h>
#include "history.h"

TEST_CASE("append stores full precision") {
    History h;
    REQUIRE(h.init(10));
    CHECK(h.maybe_append(1000, 15.52f, 66.35f, 30));
    REQUIRE(h.size() == 1);
    CHECK(h.at(0).temp_c10 == 155);
    CHECK(h.at(0).rh_c10 == 664);   // 66.35 -> 663.5 rounds to 664
    CHECK(h.at(0).t == 1000);
}

TEST_CASE("time gate rejects samples closer than min_gap_s") {
    History h;
    REQUIRE(h.init(10));
    CHECK(h.maybe_append(1000, 10.0f, 50.0f, 30));
    CHECK_FALSE(h.maybe_append(1010, 11.0f, 51.0f, 30));  // only 10s later
    CHECK_FALSE(h.maybe_append(1029, 11.0f, 51.0f, 30));  // 29s
    CHECK(h.maybe_append(1030, 12.0f, 52.0f, 30));        // exactly 30s
    CHECK(h.size() == 2);
    CHECK(h.at(1).temp_c10 == 120);
}

TEST_CASE("ring wraps and drops oldest") {
    History h;
    REQUIRE(h.init(3));
    h.maybe_append(100, 1.0f, 10.0f, 30);
    h.maybe_append(200, 2.0f, 20.0f, 30);
    h.maybe_append(300, 3.0f, 30.0f, 30);
    CHECK(h.size() == 3);
    h.maybe_append(400, 4.0f, 40.0f, 30);
    CHECK(h.size() == 3);
    CHECK(h.at(0).t == 200);       // 100 dropped
    CHECK(h.at(2).t == 400);
    CHECK(h.oldest_epoch() == 200);
    CHECK(h.newest_epoch() == 400);
}

TEST_CASE("negative temperatures survive the round trip") {
    History h;
    REQUIRE(h.init(4));
    h.maybe_append(100, -3.4f, 80.0f, 30);
    CHECK(h.at(0).temp_c10 == -34);
}

TEST_CASE("clear empties without freeing") {
    History h;
    REQUIRE(h.init(4));
    h.maybe_append(100, 1.0f, 10.0f, 30);
    h.clear();
    CHECK(h.size() == 0);
    CHECK(h.capacity() == 4);
    CHECK(h.maybe_append(101, 2.0f, 20.0f, 30));   // gate resets too
}

TEST_CASE("init(0) fails cleanly") {
    History h;
    CHECK_FALSE(h.init(0));
    CHECK(h.size() == 0);
}
