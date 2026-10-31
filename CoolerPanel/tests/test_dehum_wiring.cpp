#include <doctest/doctest.h>
#include "app.h"
#include "history.h"
#include "history_msg.h"
#include "chart.h"
#include "cooler_state.h"
#include "dehum_state.h"
#include "dehum_commands.h"
#include "test_platform_stub.h"
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

// The dehumidifier's path through app_on_mqtt_message (fixed dehum/... topics,
// its own history), dehum/history parsing, and the humidity averages that its
// graph and the Trend page's "RH avg" line draw.

static std::string load(const char* name) {
    std::ifstream f(std::string(FIXture_DIR) + "/" + name);
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

static void send(const char* topic, const std::string& body) {
    app_on_mqtt_message(topic, (const uint8_t*)body.data(), body.size());
}

static void reset_dehum() {
    app_init_history();
    dehum_state() = DehumState{};
    dehum_commands() = DehumCommands{};
}

TEST_CASE("dehum/data lands in the dehumidifier's state and trend, never the Cooler's") {
    reset_dehum();
    const size_t cooler_before = panel_history().size();
    const bool cooler_valid = cooler_state().valid;
    send("dehum/availability", "online");
    send("dehum/data", load("dehum_data_auto.json"));
    CHECK(dehum_state().valid);
    CHECK(dehum_state().online);
    REQUIRE(dehum_history().size() == 1);
    const Sample& s = dehum_history().at(0);
    CHECK(s.t == g_test_epoch);
    CHECK(s.temp_c10 == 43);             // temperature is the primary value
    CHECK(s.rh_c10 == 989);              // humidity rides along
    CHECK(s.ac == 1);                    // relay 1
    CHECK(panel_history().size() == cooler_before);
    CHECK(cooler_state().valid == cooler_valid);
}

TEST_CASE("dehum/data without a reading, or replayed while offline, adds no sample") {
    reset_dehum();
    send("dehum/data", load("dehum_data_cycle_linklost.json"));   // temp/humidity null
    CHECK(dehum_state().valid);
    CHECK(dehum_history().size() == 0);
    send("dehum/availability", "offline");
    g_test_epoch += 60;
    send("dehum/data", load("dehum_data_auto.json"));             // the retained copy
    CHECK(dehum_history().size() == 0);
    send("dehum/availability", "online");
    g_test_epoch += 60;
    send("dehum/data", load("dehum_data_auto.json"));
    CHECK(dehum_history().size() == 1);
    g_test_epoch = 1700000000;
}

TEST_CASE("dehum/data feeds the command queue's echo check") {
    reset_dehum();
    send("dehum/availability", "online");
    send("dehum/data", load("dehum_data_auto.json"));
    DehumCommands& c = dehum_commands();
    const int en = DEHUM_PLAIN_N + RF_EN + DEHUM_RULE_FIELDS;    // r2_en
    REQUIRE(c.enter(en, 1, dehum_state(), true, 0));
    c.tick(400, dehum_state(), true);
    REQUIRE(c.is_pending(en));
    std::string j = load("dehum_data_auto.json");
    const size_t at = j.find("\"r2_en\":0");
    REQUIRE(at != std::string::npos);
    j.replace(at, 9, "\"r2_en\":1");
    send("dehum/data", j);
    CHECK_FALSE(c.is_pending(en));
}

// A dehum/history window as Node-RED sends it: cooler/history's shape with
// arrays hum, temp, relay.
static std::string window(int64_t t0, int n) {
    std::string hum = "[", temp = "[", relay = "[";
    for (int i = 0; i < n; i++) {
        const char* sep = i ? "," : "";
        hum += sep + std::to_string(90 + i % 5);
        temp += sep + std::string(i == 2 ? "null" : "4.5");
        relay += sep + std::to_string(i % 2);
    }
    return "{\"v\":1,\"t0\":" + std::to_string(t0) + ",\"interval_s\":60,\"hum\":" + hum +
           "],\"temp\":" + temp + "],\"relay\":" + relay + "]}";
}

TEST_CASE("dehum/history parses like cooler/history and fills the dehumidifier's trend") {
    reset_dehum();
    const std::string w = window(g_test_epoch - 10 * 60, 10);
    HistoryMsg m;
    REQUIRE(history_msg_parse(w.data(), w.size(), g_test_epoch, m));
    REQUIRE(m.samples.size() == 9);          // the slot without a temperature is left out
    CHECK(m.samples[0].temp_c10 == 45);
    CHECK(m.samples[0].rh_c10 == 900);
    CHECK(m.samples[1].ac == 1);
    CHECK(m.samples[2].t == m.samples[0].t + 3 * 60);

    const size_t cooler_before = panel_history().size();
    send("dehum/history", w);
    CHECK(dehum_history().size() == 9);
    CHECK(panel_history().size() == cooler_before);
    send("dehum/history", "{\"v\":2}");      // not a window: ignored
    CHECK(dehum_history().size() == 9);
}

TEST_CASE("humidity averages: the same time-weighting over the humidity") {
    History h;
    REQUIRE(h.init(1000));
    const int64_t t0 = 1700000000;
    // An hour and a bit at 60 s: temperature flat 4, humidity a ramp 80 -> 90.
    for (int i = 0; i <= 70; i++)
        h.maybe_append(t0 + i * 60, 4.0f, 80.0f + 10.0f * (float)(i > 60 ? 60 : i) / 60.0f, 0);
    float m = 0;
    REQUIRE(chart_window_mean(h, t0, t0 + 3600, m, AvgOf::Rh));
    CHECK(m == doctest::Approx(85.0).epsilon(0.002));
    REQUIRE(chart_window_mean(h, t0, t0 + 3600, m));          // the temperature default
    CHECK(m == doctest::Approx(4.0));
    REQUIRE(chart_latest_avg(h, t0 + 3600, m, AvgOf::Rh));
    CHECK(m == doctest::Approx(85.0).epsilon(0.002));
    CHECK_FALSE(chart_latest_avg(h, t0 + 3000, m, AvgOf::Rh));  // not a whole hour behind
    float avg[4]; bool has[4];
    chart_rolling_avg(h, t0 + 3600, t0 + 4200, 4, avg, has, AvgOf::Rh);
    CHECK(has[3]);
    CHECK(avg[3] > 85.0f);                                     // the ramp's later hour
}

TEST_CASE("the RH avg trace shows by default and toggles on its own") {
    SeriesShown s;
    CHECK(s.rh_avg);
    series_toggle(s, Series::RhAvg);
    CHECK_FALSE(s.rh_avg);
    CHECK(s.temp);
    CHECK(s.rh);
    CHECK(s.avg);
    series_toggle(s, Series::RhAvg);
    CHECK(s.rh_avg);
}
