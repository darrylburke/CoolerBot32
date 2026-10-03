#include "config.h"
#include "app.h"
#include "platform.h"
#include "mqtt_router.h"
#include "cooler_defaults.h"   // COOLER_DEFAULT_MQTT_CA
#include <mosquitto.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

// Single-threaded: no mosquitto_loop_start() background thread. The
// connection is driven entirely by mqtt_poll(), called once per main-loop
// tick (after lv_timer_handler() in sim/main.cpp) -- so on_connect/
// on_disconnect/on_message all fire synchronously on the main/UI thread and
// never race with ui_refresh()/shared-state access. No mutex needed.
static struct mosquitto* g_mosq = nullptr;
static uint32_t g_last_reconnect_ms = 0;
static std::string g_base;

static void on_connect(struct mosquitto* m, void*, int rc) {
    if (rc == 0) {
        // The same exact topics the device subscribes to. The panel's
        // broker login may read only these (no <base>/# wildcard).
        mosquitto_subscribe(m, nullptr, (g_base + "/data").c_str(), 1);
        mosquitto_subscribe(m, nullptr, (g_base + "/availability").c_str(), 1);
        mosquitto_subscribe(m, nullptr, (g_base + "/history").c_str(), 0);
        app_set_link_state(LINK_OK);
    }
    else { fprintf(stderr, "mqtt connect rc=%d\n", rc); app_set_link_state(LINK_MQTT_DOWN); }
}
static void on_disconnect(struct mosquitto*, void*, int) { app_set_link_state(LINK_MQTT_DOWN); }
static void on_message(struct mosquitto*, void*, const struct mosquitto_message* msg) {
    app_on_mqtt_message(msg->topic, (const uint8_t*)msg->payload, (size_t)msg->payloadlen);
}

void mqtt_start(const SimConfig& c) {
    mosquitto_lib_init();
    struct mosquitto* m = mosquitto_new("cooler-panel-sim", true, nullptr);
    mosquitto_username_pw_set(m, c.user.c_str(), c.pass.c_str());
    g_base = c.base;
    router_set_prefix(c.base.c_str());
    if (c.port == 8883) {
        // libmosquitto wants the CA as a file: write the compiled-in PEM once.
        char path[] = "/tmp/cooler-panel-ca-XXXXXX";
        int fd = mkstemp(path);
        if (fd >= 0) {
            (void)!write(fd, COOLER_DEFAULT_MQTT_CA, sizeof(COOLER_DEFAULT_MQTT_CA) - 1);
            close(fd);
            if (mosquitto_tls_set(m, path, nullptr, nullptr, nullptr, nullptr) != MOSQ_ERR_SUCCESS)
                fprintf(stderr, "mqtt: TLS setup failed\n");
        }
    }
    mosquitto_connect_callback_set(m, on_connect);
    mosquitto_disconnect_callback_set(m, on_disconnect);
    mosquitto_message_callback_set(m, on_message);
    app_set_link_state(LINK_MQTT_DOWN);
    if (mosquitto_connect(m, c.host.c_str(), c.port, 60) != MOSQ_ERR_SUCCESS)
        fprintf(stderr, "mqtt: initial connect failed (will retry)\n");
    g_mosq = m;
}

// Pumps the mosquitto network loop on the calling (main/UI) thread. No-op
// until mqtt_start() has run (interactive mode only -- headless/replay never
// calls mqtt_start(), so this returns immediately every tick).
void mqtt_poll() {
    if (!g_mosq) return;
    int rc = mosquitto_loop(g_mosq, 0, 1);
    if (rc == MOSQ_ERR_CONN_LOST || rc == MOSQ_ERR_NO_CONN || rc == MOSQ_ERR_CONN_REFUSED) {
        uint32_t now = platform_now_ms();
        if (now - g_last_reconnect_ms >= 2000) {
            g_last_reconnect_ms = now;
            mosquitto_reconnect(g_mosq);
        }
    }
}

extern "C" bool platform_mqtt_publish(const char* topic, const char* payload,
                                      size_t len, bool retain) {
    if (!g_mosq) return false;
    int rc = mosquitto_publish(g_mosq, nullptr, topic, (int)len, payload, 1, retain);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "mqtt: publish to %s failed rc=%d\n", topic, rc);
        return false;
    }
    return true;
}
