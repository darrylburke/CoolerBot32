#include "mqtt_pubsub.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <lwip/netdb.h>
#include "app.h"
#include "cooler_defaults.h"            // COOLER_DEFAULT_MQTT_CA

static DeviceConfig s_cfg;              // owns the strings the connection points at
static WiFiClient s_plain;
// Port 8883 = TLS, verified against the CA compiled in from the controller's
// config (tools/import_cooler_broker.sh). WiFiClientSecure checks both the
// certificate chain and that the certificate names the host we dialled, so
// the panel only ever talks to the real broker.
static WiFiClientSecure s_tls;
static PubSubClient s_mqtt;
static std::string s_client_id;
static uint32_t s_next_attempt_ms = 0;
static uint32_t s_backoff_ms = 1000;    // 1 s -> 60 s (spec §6)

// lwIP's getaddrinfo() runs the lookup on the TCPIP thread and leaves the DNS
// cache alone. Arduino 3.2.0's hostByName() -- what client.connect(host, port)
// uses -- clears lwIP's DNS cache from the calling task; if SNTP's lookup of
// pool.ntp.org is still pending, that runs SNTP's callback outside the TCPIP
// core lock and lwIP asserts ("sys_untimeout ... Required to lock TCPIP core
// functionality!"). After a crash reboot the clock survives the soft reset,
// so this can happen on any boot, not just the first.
static bool resolve(const char* host, IPAddress& out) {
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(host, nullptr, &hints, &res) != 0 || !res) return false;
    out = IPAddress(((struct sockaddr_in*)res->ai_addr)->sin_addr.s_addr);
    freeaddrinfo(res);
    return true;
}

static void on_message(char* topic, uint8_t* payload, unsigned int len) {
    app_on_mqtt_message(topic, payload, (size_t)len);
}

void mqtt_begin(const DeviceConfig& c) {
    s_cfg = c;
    s_client_id = "cooler-panel-" + std::string(String((uint32_t)(ESP.getEfuseMac() >> 32), HEX).c_str());
    if (s_cfg.mqtt_port == 8883) {
        s_tls.setCACert(COOLER_DEFAULT_MQTT_CA);
        s_tls.setHandshakeTimeout(15);      // seconds
        s_mqtt.setClient(s_tls);
    } else {
        s_mqtt.setClient(s_plain);
    }
    s_mqtt.setServer(s_cfg.mqtt_host.c_str(), s_cfg.mqtt_port);
    s_mqtt.setBufferSize(24576);            // /history from Node-RED is ~14 KB
    s_mqtt.setKeepAlive(30);
    s_mqtt.setCallback(on_message);
    app_set_link_state(LINK_MQTT_DOWN);
    s_next_attempt_ms = 0;                  // connect on first poll
}

void mqtt_poll() {
    if (WiFi.status() != WL_CONNECTED) {
        app_set_link_state(LINK_WIFI_DOWN);
        return;
    }
    if (s_mqtt.connected()) {
        s_mqtt.loop();
        return;
    }
    app_set_link_state(LINK_MQTT_DOWN);
    uint32_t now = millis();
    if (now < s_next_attempt_ms) return;
    // Hold the first connect until SNTP has set the clock: TLS needs the right
    // time to check the broker's certificate, so an earlier attempt can only
    // fail. Give up waiting after kClockWaitMs so a blocked NTP cannot strand
    // MQTT.
    static constexpr uint32_t kClockWaitMs = 20000;
    static uint32_t s_wifi_up_ms = 0;
    if (s_wifi_up_ms == 0) s_wifi_up_ms = now ? now : 1;
    if (time(nullptr) < 1600000000 && now - s_wifi_up_ms < kClockWaitMs) return;
    Serial.printf("mqtt: connecting to %s:%u%s as %s...\n", s_cfg.mqtt_host.c_str(),
                  s_cfg.mqtt_port, s_cfg.mqtt_port == 8883 ? " (TLS)" : "",
                  s_cfg.mqtt_user.c_str());
    // Blocking (TCP + CONNECT); UI freezes briefly on reconnect attempts,
    // which the backoff keeps rare. The socket is opened here, on an address
    // from resolve(), and PubSubClient::connect() reuses an already-connected
    // client, so hostByName() is never called. TLS is still given the host
    // name: the certificate is checked against it, not against the address.
    IPAddress ip;
    const bool resolved = resolve(s_cfg.mqtt_host.c_str(), ip);
    const bool open = resolved &&
        (s_cfg.mqtt_port == 8883
             ? s_tls.connect(ip, s_cfg.mqtt_port, s_cfg.mqtt_host.c_str(),
                             COOLER_DEFAULT_MQTT_CA, nullptr, nullptr) == 1
             : s_plain.connect(ip, s_cfg.mqtt_port) == 1);
    if (!resolved) Serial.printf("mqtt: cannot resolve %s\n", s_cfg.mqtt_host.c_str());
    if (open && s_mqtt.connect(s_client_id.c_str(), s_cfg.mqtt_user.c_str(),
                               s_cfg.mqtt_pass.c_str())) {
        // Explicit subscriptions rather than a "<base>/#" wildcard: the
        // panel only needs /data, /availability and /history, and a
        // wildcard would also pick up any /cmd echo or future subtree this
        // device itself publishes to. A broker ACL without /history just
        // never delivers it; the panel then trends from live /data alone.
        s_mqtt.subscribe((s_cfg.mqtt_base + "/data").c_str(), 1);
        s_mqtt.subscribe((s_cfg.mqtt_base + "/availability").c_str(), 1);
        s_mqtt.subscribe((s_cfg.mqtt_base + "/history").c_str(), 0);
        app_set_link_state(LINK_OK);
        s_backoff_ms = 1000;
        Serial.println("mqtt: connected + subscribed");
    } else {
        char tls_err[96] = "";
        if (s_cfg.mqtt_port == 8883) s_tls.lastError(tls_err, sizeof(tls_err));
        Serial.printf("mqtt: connect failed state=%d%s%s, retry in %lu ms\n",
                      s_mqtt.state(), tls_err[0] ? " tls: " : "", tls_err,
                      (unsigned long)s_backoff_ms);
        s_next_attempt_ms = now + s_backoff_ms;
        s_backoff_ms = min<uint32_t>(s_backoff_ms * 2, 60000);
    }
}

extern "C" bool platform_mqtt_publish(const char* topic, const char* payload,
                                      size_t len, bool retain) {
    if (!s_mqtt.connected()) return false;
    return s_mqtt.publish(topic, (const uint8_t*)payload, (unsigned int)len, retain);
}
