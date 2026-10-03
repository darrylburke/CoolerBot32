#include <doctest/doctest.h>
#include "history_msg.h"
#include "history.h"
#include <cstdio>
#include <string>

static const int64_t NOW = 1700000000;
static const int64_t T0 = 1699999380;   // a minute boundary, 620 s before NOW

static std::string body(int64_t t0 = T0, const char* temp = "4.2,null,4.4",
                        const char* hum = "80,81,82", const char* relay = "0,1,1",
                        int v = 1, int interval = 60) {
    char b[1024];
    std::snprintf(b, sizeof b,
                  "{\"v\":%d,\"t0\":%lld,\"interval_s\":%d,\"temp\":[%s],\"hum\":[%s],\"relay\":[%s]}",
                  v, (long long)t0, interval, temp, hum, relay);
    return b;
}

static bool parse(const std::string& s, HistoryMsg& m, int64_t now = NOW) {
    return history_msg_parse(s.data(), s.size(), now, m);
}

TEST_CASE("parses slots and skips those without temperature or humidity") {
    HistoryMsg m;
    REQUIRE(parse(body(T0, "4.2,null,4.4,5.0", "80,81,82,null", "0,1,1,0"), m));
    CHECK(m.from == T0);
    CHECK(m.to == T0 + 240);
    REQUIRE(m.samples.size() == 2);
    CHECK(m.samples[0].t == T0);
    CHECK(m.samples[0].temp_c10 == 42);
    CHECK(m.samples[0].rh_c10 == 800);
    CHECK(m.samples[0].ac == 0);
    CHECK(m.samples[1].t == T0 + 120);
    CHECK(m.samples[1].temp_c10 == 44);
    CHECK(m.samples[1].ac == 1);
}

TEST_CASE("rejects anything that is not a sound v1 window") {
    HistoryMsg m;
    CHECK_FALSE(parse(body(T0, "4.2", "80", "0", 2), m));            // version
    CHECK_FALSE(parse(body(T0, "4.2", "80", "0", 1, 30), m));        // interval
    CHECK_FALSE(parse(body(T0, "4.2,4.3", "80", "0,0"), m));         // unequal
    CHECK_FALSE(parse(body(T0, "", "", ""), m));                     // empty
    CHECK_FALSE(parse(body(12345, "4.2", "80", "0"), m));            // controller-side clock unset
    CHECK_FALSE(parse(body(NOW + 600, "4.2", "80", "0"), m));        // from the future
    const std::string junk = "not json";
    CHECK_FALSE(parse(junk, m));
    const std::string partial = "{\"v\":1,\"t0\":1699999380,\"interval_s\":60,\"temp\":[4]}";
    CHECK_FALSE(parse(partial, m));
}

TEST_CASE("future check is skipped while the panel clock is unset") {
    HistoryMsg m;
    CHECK(parse(body(NOW + 600, "4.2", "80", "0"), m, 0));
}

TEST_CASE("coverage: a message adds something only where the panel has no sample") {
    HistoryMsg m;
    REQUIRE(parse(body(), m));          // samples at T0 and T0+120; T0+60 is null

    History empty;
    REQUIRE(empty.init(10));
    CHECK(history_adds_coverage(empty, m));

    History full;
    REQUIRE(full.init(10));
    full.maybe_append(T0 + 10, 4.0f, 80.0f, 30);
    full.maybe_append(T0 + 130, 4.0f, 80.0f, 30);
    CHECK_FALSE(history_adds_coverage(full, m));   // T0+60 is missing on both sides

    History gap;
    REQUIRE(gap.init(10));
    gap.maybe_append(T0 + 10, 4.0f, 80.0f, 30);
    CHECK(history_adds_coverage(gap, m));          // panel lacks T0+120
}
