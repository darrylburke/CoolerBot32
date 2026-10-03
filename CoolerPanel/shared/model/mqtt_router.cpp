#include "mqtt_router.h"
#include "cooler_state.h"
#include <cstring>

static char s_prefix[64] = "cooler";   // overwritten from config at startup

void router_set_prefix(const char* prefix) {
    if (!prefix) return;
    std::strncpy(s_prefix, prefix, sizeof(s_prefix) - 1);
    s_prefix[sizeof(s_prefix) - 1] = 0;
}
const char* router_prefix(void) { return s_prefix; }

bool router_is_leaf(const char* topic, const char* leaf) {
    if (!topic || !leaf) return false;
    size_t pn = std::strlen(s_prefix);
    return std::strncmp(topic, s_prefix, pn) == 0 && topic[pn] == '/' &&
           std::strcmp(topic + pn + 1, leaf) == 0;
}

bool route_message(CoolerState& s, const char* topic, const char* payload,
                   size_t len, int64_t now_epoch) {
    if (!topic || !payload) return false;
    size_t pn = std::strlen(s_prefix);
    if (std::strncmp(topic, s_prefix, pn) != 0) return false;
    if (topic[pn] != '/') return false;
    const char* leaf = topic + pn + 1;

    if (std::strcmp(leaf, "data") == 0)
        return cooler_parse_data(s, payload, len, now_epoch);

    if (std::strcmp(leaf, "availability") == 0) {
        s.online = (len == 6 && std::strncmp(payload, "online", 6) == 0);
        s.availability_seen = true;
        return true;
    }
    // /ac1 and /ac2 are deliberately not handled -- every field they carry
    // already exists in /data, and two sources for one fact invites drift.
    return false;
}
