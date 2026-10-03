#include <doctest/doctest.h>
#include "history_msg.h"
#include "history.h"
#include "test_platform_stub.h"
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

static std::vector<Sample> missing(const History& h, const HistoryMsg& m) {
    std::vector<Sample> out;
    history_missing(h, m, out);
    return out;
}

static std::string minutes(int n, const char* v) {
    std::string s;
    for (int i = 0; i < n; i++) { if (i) s += ","; s += v; }
    return s;
}

TEST_CASE("missing: an empty panel takes every history sample") {
    HistoryMsg m;
    REQUIRE(parse(body(), m));          // samples at T0 and T0+120; T0+60 is null
    History h;
    REQUIRE(h.init(100));
    CHECK(missing(h, m).size() == 2);
}

TEST_CASE("missing: a panel sampling every 30 s needs nothing, even with clock skew") {
    // 10 minutes of history; the panel's clock runs 3 s behind Node-RED's.
    HistoryMsg m;
    const std::string v = minutes(10, "4.0");
    REQUIRE(parse(body(T0 - 600, v.c_str(), minutes(10, "80").c_str(), minutes(10, "0").c_str()), m));
    History h;
    REQUIRE(h.init(100));
    for (int64_t t = T0 - 600 - 3; t < T0; t += 30) h.maybe_append(t, 4.0f, 80.0f, 30);
    CHECK(missing(h, m).empty());
}

TEST_CASE("missing: the newest minutes belong to live data") {
    // The panel has not sampled the current minute yet; history has.
    HistoryMsg m;
    REQUIRE(parse(body(T0, "4.0,4.1,4.2", "80,80,80", "0,0,0"), m));
    History h;
    REQUIRE(h.init(100));
    h.maybe_append(T0 + 5, 4.0f, 80.0f, 30);
    h.maybe_append(T0 + 35, 4.0f, 80.0f, 30);
    h.maybe_append(T0 + 65, 4.0f, 80.0f, 30);
    h.maybe_append(T0 + 95, 4.0f, 80.0f, 30);
    CHECK(missing(h, m).empty());       // T0+120 is newer than the panel's newest
}

TEST_CASE("missing: after a reboot only the minutes before the panel's own data are taken") {
    HistoryMsg m;
    const std::string v = minutes(10, "4.0");
    REQUIRE(parse(body(T0 - 600, v.c_str(), minutes(10, "80").c_str(), minutes(10, "0").c_str()), m));
    History h;
    REQUIRE(h.init(100));
    h.maybe_append(T0 - 110, 4.0f, 80.0f, 30);   // up for the last two minutes
    h.maybe_append(T0 - 80, 4.0f, 80.0f, 30);
    h.maybe_append(T0 - 50, 4.0f, 80.0f, 30);
    h.maybe_append(T0 - 20, 4.0f, 80.0f, 30);
    std::vector<Sample> got = missing(h, m);
    REQUIRE(got.size() == 7);                    // T0-600 .. T0-240
    CHECK(got.front().t == T0 - 600);
    CHECK(got.back().t == T0 - 240);
}

TEST_CASE("missing: samples stamped before the panel's clock was set do not count") {
    HistoryMsg m;
    REQUIRE(parse(body(), m));
    History h;
    REQUIRE(h.init(100));
    h.maybe_append(5, 4.0f, 80.0f, 30);          // epoch ~0: SNTP had not synced
    CHECK(missing(h, m).size() == 2);
}

TEST_CASE("the parse takes its memory from the platform's large-buffer allocator") {
    // On the device that is PSRAM: a ~14 KB /history twice a minute must not
    // churn the internal SRAM Wi-Fi and TLS live in.
    const std::string v = minutes(100, "4.0");
    const std::string b = body(T0 - 6000, v.c_str(), minutes(100, "80").c_str(), minutes(100, "0").c_str());
    g_big_allocs = 0;
    HistoryMsg m;
    REQUIRE(parse(b, m));
    CHECK(g_big_allocs > 0);
}

