#include <doctest/doctest.h>
#include "cooler_state.h"
#include "mqtt_router.h"
#include <string>
#include <fstream>
#include <sstream>
#include <cstring>

static std::string load(const char* name) {
    std::ifstream f(std::string(FIXture_DIR) + "/" + name);
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

static CoolerState parsed(const char* name, int64_t now = 1000) {
    CoolerState s;
    auto j = load(name);
    REQUIRE(cooler_parse_data(s, j.c_str(), j.size(), now));
    return s;
}

TEST_CASE("parses a v2 /data payload") {
    CoolerState s = parsed("data_normal.json", 1790000100);
    CHECK(s.valid);
    CHECK(s.last_rx_epoch == 1790000100);
    CHECK(s.coolerset == 4);
    CHECK(s.range == 2);
    CHECK(s.fin_cutoff == 0);
    CHECK(s.fin_recover == 3);
    CHECK(s.settle == 10);
    CHECK(s.minofftime == 5);
    CHECK(s.minruntime == 180);
    CHECK(s.maxrun == 10);
    CHECK(s.dutypercent == 50);
    CHECK(s.sampleinterval == 3600);
    CHECK(s.temp_valid);
    CHECK(s.temp == doctest::Approx(4.8));
    CHECK(s.humidity == doctest::Approx(78.2));
    CHECK(s.fin_temp_valid);
    CHECK(s.fin_temp == doctest::Approx(1.6));
    CHECK(s.fin_ohms == doctest::Approx(28410));
    CHECK(s.fin_slope == doctest::Approx(-0.8));
    CHECK(std::string(s.mode) == "normal");
    CHECK(std::string(s.override_src) == "none");
    CHECK(std::string(s.state) == "cooling");
    CHECK(s.relay == 1);
    CHECK(s.cool_call == 1);
    CHECK(s.compressor == 1);
    CHECK(s.run_s == 142u);
    CHECK(s.hold_s == 38u);
    CHECK(s.fin_cal == 1);
    CHECK(s.fin_beta == doctest::Approx(3912));
    CHECK(s.fin_cal_err_valid);
    CHECK(s.uptime_s == 86400u);
    CHECK_FALSE(cooler_in_override(s));
}

TEST_CASE("a v3 payload (no schema version) is rejected") {
    CoolerState s = parsed("data_normal.json", 100);
    auto v3 = load("data_v3.json");
    CHECK_FALSE(cooler_parse_data(s, v3.c_str(), v3.size(), 200));
    CHECK(s.last_rx_epoch == 100);
    CHECK(std::string(s.state) == "cooling");   // untouched
}

TEST_CASE("a payload with the wrong schema version is rejected") {
    CoolerState s;
    const char* v3ish = R"({"v":3,"temp":4.0})";
    CHECK_FALSE(cooler_parse_data(s, v3ish, std::strlen(v3ish), 1));
    CHECK_FALSE(s.valid);
}

TEST_CASE("malformed payload is rejected and leaves prior state intact") {
    CoolerState s = parsed("data_normal.json", 100);
    auto bad = load("data_malformed.json");
    CHECK_FALSE(cooler_parse_data(s, bad.c_str(), bad.size(), 200));
    CHECK(s.coolerset == 4);
    CHECK(s.last_rx_epoch == 100);
}

TEST_CASE("null sensor values mark invalid and keep the last reading") {
    CoolerState s = parsed("data_normal.json", 100);
    auto j = load("data_blind.json");
    REQUIRE(cooler_parse_data(s, j.c_str(), j.size(), 200));
    CHECK_FALSE(s.temp_valid);
    CHECK(s.temp == doctest::Approx(4.8));        // last good value kept, not zeroed
    CHECK_FALSE(s.humidity_valid);
    CHECK_FALSE(s.fin_temp_valid);
    CHECK(s.compressor == -1);
    CHECK(s.sht_fault == 1);
    CHECK(s.fin_fault == 1);
    CHECK(std::string(s.mode) == "blind");
}

TEST_CASE("absent keys keep prior values; a later valid reading restores validity") {
    CoolerState s = parsed("data_blind.json", 100);
    const char* sparse = R"({"v":2,"coolerset":6})";
    REQUIRE(cooler_parse_data(s, sparse, std::strlen(sparse), 200));
    CHECK(s.coolerset == 6);
    CHECK(std::string(s.mode) == "blind");        // absent -> unchanged
    CHECK_FALSE(s.temp_valid);                     // absent -> unchanged
    const char* back = R"({"v":2,"temp":5.5})";
    REQUIRE(cooler_parse_data(s, back, std::strlen(back), 300));
    CHECK(s.temp_valid);
    CHECK(s.temp == doctest::Approx(5.5));
}

TEST_CASE("override fixtures") {
    CoolerState s = parsed("data_override.json");
    CHECK(cooler_in_override(s));
    CHECK(std::string(s.override_src) == "switch");
    const char* proxy = R"({"v":2,"mode":"override-proxy"})";
    REQUIRE(cooler_parse_data(s, proxy, std::strlen(proxy), 2));
    CHECK(cooler_in_override(s));
}

TEST_CASE("defrost, no-response and calibrating fixtures") {
    CoolerState d = parsed("data_defrost.json");
    CHECK(d.defrost == 1);
    CHECK(std::string(d.state) == "defrost");
    CHECK(d.fin_temp == doctest::Approx(-0.4));

    CoolerState n = parsed("data_noresponse.json");
    CHECK(n.no_response == 1);
    CHECK(n.relay == 1);
    CHECK(n.compressor == 0);

    CoolerState c = parsed("data_calibrating.json");
    CHECK(c.cal_active == 1);
    CHECK(c.cal_points == 3);
    CHECK(c.cal_span == doctest::Approx(6.2));
    CHECK(c.fin_cal == 0);
    CHECK_FALSE(c.fin_cal_err_valid);
}

TEST_CASE("fin-proxy fixture") {
    CoolerState s = parsed("data_finproxy.json");
    CHECK(std::string(s.mode) == "fin-proxy");
    CHECK(std::string(s.state) == "rest");
    CHECK(s.hold_s == 360u);
    CHECK_FALSE(s.temp_valid);
    CHECK(s.fin_temp_valid);
}

TEST_CASE("router dispatches data and availability") {
    router_set_prefix("ha/esp32-cooler");
    CoolerState s;
    auto j = load("data_normal.json");
    CHECK(route_message(s, "ha/esp32-cooler/data", j.c_str(), j.size(), 500));
    CHECK(s.coolerset == 4);

    CHECK(route_message(s, "ha/esp32-cooler/availability", "online", 6, 501));
    CHECK(s.online == true);
    CHECK(route_message(s, "ha/esp32-cooler/availability", "offline", 7, 502));
    CHECK(s.online == false);
}

TEST_CASE("router ignores unrelated and legacy per-unit topics") {
    router_set_prefix("ha/esp32-cooler");
    CoolerState s;
    CHECK_FALSE(route_message(s, "ha/esp32-cooler/ac1", "{}", 2, 0));
    CHECK_FALSE(route_message(s, "ha/other/data", "{}", 2, 0));
    CHECK_FALSE(route_message(s, "totally/unrelated", "{}", 2, 0));
}

TEST_CASE("the end of a calibration run is classified: calibrated, rejected, or no fit") {
    auto run_then = [](const char* end) {
        CoolerState s = parsed("data_calibrating.json");   // cal_active 1, beta 3950, err null
        REQUIRE(s.cal_result == CAL_RESULT_NONE);
        REQUIRE(cooler_parse_data(s, end, std::strlen(end), 2));
        return s.cal_result;
    };
    CHECK(run_then(R"({"v":2,"cal_active":0,"fin_cal":1,"fin_beta":3600,"fin_r0":12000,"fin_cal_err":0.2})")
          == CAL_RESULT_CALIBRATED);
    CHECK(run_then(R"({"v":2,"cal_active":0,"fin_cal":0,"fin_beta":3950,"fin_r0":10000,"fin_cal_err":1.4})")
          == CAL_RESULT_REJECTED);
    CHECK(run_then(R"({"v":2,"cal_active":0,"fin_cal":0,"fin_beta":3950,"fin_r0":10000,"fin_cal_err":null})")
          == CAL_RESULT_NO_FIT);

    CoolerState s = parsed("data_calibrating.json");
    const char* done = R"({"v":2,"cal_active":0,"fin_cal":1,"fin_beta":3600})";
    REQUIRE(cooler_parse_data(s, done, std::strlen(done), 2));
    const char* again = R"({"v":2,"cal_active":1})";
    REQUIRE(cooler_parse_data(s, again, std::strlen(again), 3));
    CHECK(s.cal_result == CAL_RESULT_NONE);           // a new run clears the verdict
}

TEST_CASE("a null fin_slope is marked invalid") {
    CHECK(parsed("data_normal.json").fin_slope_valid);
    CHECK_FALSE(parsed("data_blind.json").fin_slope_valid);
}
