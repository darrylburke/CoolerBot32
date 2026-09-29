#include <doctest/doctest.h>
#include "app.h"
#include "history.h"
#include "mqtt_router.h"
#include <string>
#include <fstream>
#include <sstream>
#include <cstring>

// Covers the app.cpp wiring itself (app_on_mqtt_message / panel_history() /
// app_init_history()) -- test_history.cpp and test_chart.cpp only exercise
// History/chart_downsample in isolation, never the glue that feeds real MQTT
// messages into history.

static std::string load(const char* name) {
    std::ifstream f(std::string(FIXture_DIR) + "/" + name);
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

TEST_CASE("app_on_mqtt_message samples /data into history but not /availability") {
    // Fresh, fully-reset history: app_init_history() reallocates the buffer
    // (count_ = 0), so this doesn't depend on -- or interfere with -- any
    // other test case's use of the same process-wide globals.
    app_init_history();
    REQUIRE(panel_history().size() == 0);

    const std::string prefix = router_prefix();

    // A real /data payload must land exactly one sample.
    const std::string data_topic = prefix + "/data";
    const std::string data_json = load("data_normal.json");
    app_on_mqtt_message(data_topic.c_str(),
                        (const uint8_t*)data_json.data(), data_json.size());
    CHECK(panel_history().size() == 1);

    // /availability is retained and gets redelivered on every MQTT
    // reconnect (WiFi blip, broker restart...) -- it must NOT insert a
    // second, stale-data sample. This is deliberately independent of the
    // history ring's 30 s time gate (platform_epoch_utc() drives real
    // wall-clock timestamps, which a unit test shouldn't depend on): the
    // fix under test is the /data-only leaf check in app_on_mqtt_message,
    // so the assertion is simply that the count did not grow at all.
    const std::string avail_topic = prefix + "/availability";
    const char* avail_payload = "online";
    app_on_mqtt_message(avail_topic.c_str(), (const uint8_t*)avail_payload,
                        std::strlen(avail_payload));
    CHECK(panel_history().size() == 1);
}

TEST_CASE("a /data with temp:null does not add a history sample") {
    app_init_history();
    const std::string topic = std::string(router_prefix()) + "/data";
    const std::string blind = load("data_blind.json");
    app_on_mqtt_message(topic.c_str(), (const uint8_t*)blind.data(), blind.size());
    CHECK(panel_history().size() == 0);
}

TEST_CASE("the relay state is recorded with each sample") {
    app_init_history();
    const std::string topic = std::string(router_prefix()) + "/data";
    const std::string cooling = load("data_normal.json");     // relay 1
    app_on_mqtt_message(topic.c_str(), (const uint8_t*)cooling.data(), cooling.size());
    REQUIRE(panel_history().size() == 1);
    CHECK(panel_history().at(0).ac == 1);
}
