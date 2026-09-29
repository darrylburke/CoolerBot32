// Test-only implementation of the publish seam. Records every publish so
// command tests can assert on exactly what would have gone to the broker.
#include "test_publish_stub.h"
#include "platform.h"

static std::vector<PubRecord> g_pubs;
static bool g_fail = false;

std::vector<PubRecord>& test_publishes() { return g_pubs; }
void test_publish_reset(bool fail) { g_pubs.clear(); g_fail = fail; }

extern "C" bool platform_mqtt_publish(const char* topic, const char* payload,
                                      size_t len, bool retain) {
    if (g_fail) return false;
    g_pubs.push_back({std::string(topic), std::string(payload, len), retain});
    return true;
}
