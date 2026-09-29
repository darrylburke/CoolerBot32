#include "device_config.h"
#include <ArduinoJson.h>

std::string config_to_json(const DeviceConfig& c) {
    JsonDocument d;
    d["wifi"]["ssid"] = c.wifi_ssid;
    d["wifi"]["password"] = c.wifi_pass;
    auto m = d["mqtt"].to<JsonObject>();
    m["host"] = c.mqtt_host;
    m["port"] = c.mqtt_port;
    m["username"] = c.mqtt_user;
    m["password"] = c.mqtt_pass;
    m["topic_prefix"] = c.mqtt_base;
    d["configured"] = c.configured;
    std::string out;
    serializeJson(d, out);
    return out;
}

bool config_from_json(const std::string& json, DeviceConfig& out) {
    JsonDocument d;
    if (deserializeJson(d, json)) return false;
    out.wifi_ssid = d["wifi"]["ssid"] | out.wifi_ssid;
    out.wifi_pass = d["wifi"]["password"] | out.wifi_pass;
    JsonObjectConst m = d["mqtt"];
    out.mqtt_host = m["host"] | out.mqtt_host;
    out.mqtt_port = m["port"] | out.mqtt_port;
    out.mqtt_user = m["username"] | out.mqtt_user;
    out.mqtt_pass = m["password"] | out.mqtt_pass;
    out.mqtt_base = m["topic_prefix"] | (m["base_topic"] | out.mqtt_base);
    // "tls"/"ca_pem" are no longer read: plain MQTT is the only mode now.
    // A config saved by the old TLS build still has both keys in NVS --
    // they're simply ignored (unknown-key tolerance, not a rejection) so
    // that stored config still loads instead of failing outright.
    out.configured = d["configured"] | out.configured;
    return true;
}

ConfigError validate_config(const DeviceConfig& c) {
    if (c.wifi_ssid.empty()) return {false, "Wi-Fi SSID is required"};
    if (c.mqtt_host.empty()) return {false, "MQTT host is required"};
    if (c.mqtt_port == 0)    return {false, "MQTT port must be 1-65535"};
    return {true, ""};
}

BootMode decide_boot_mode(bool touch_held, bool has_config, bool wifi_connected) {
    if (touch_held || !has_config || !wifi_connected) return BootMode::Portal;
    return BootMode::Normal;
}
