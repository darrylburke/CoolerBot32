// Simulator-only: route a captured /data payload into the app as if the
// broker had delivered it. Used by --fixture for deterministic screenshots of
// each controller state (tests/fixtures/data_*.json).
#include "app.h"
#include "mqtt_router.h"
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
