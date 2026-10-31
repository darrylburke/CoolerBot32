// Simulator-only: route a captured /data payload into the app as if the
// broker had delivered it. Used by --fixture for deterministic screenshots of
// each controller state (tests/fixtures/data_*.json).
#include "app.h"
#include "mqtt_router.h"
#include "dehum_state.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

bool sim_feed_fixture(const char* path) {
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    const std::string body = ss.str();
    const std::string prefix = router_prefix();
    const std::string avail = prefix + "/availability";
    const std::string data = prefix + "/data";
    app_on_mqtt_message(avail.c_str(), (const uint8_t*)"online", 6);
    app_on_mqtt_message(data.c_str(), (const uint8_t*)body.data(), body.size());
    return true;
}

// The dehumidifier's counterpart (--dehum-fixture): an "online"
// dehum/availability then one captured dehum/data (tests/fixtures/dehum_*.json).
// The broker link is reported up, as it would be for a delivered message, so
// the Temp/Humidity page judges the dehumidifier online rather than
// "Broker unreachable". Nothing is ever published: no broker is connected.
bool sim_feed_dehum_fixture(const char* path) {
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    const std::string body = ss.str();
    app_set_link_state(LINK_OK);
    app_on_mqtt_message(DEHUM_TOPIC_AVAILABILITY, (const uint8_t*)"online", 6);
    app_on_mqtt_message(DEHUM_TOPIC_DATA, (const uint8_t*)body.data(), body.size());
    return true;
}
