#pragma once
#include <string>
#include <cstdint>
#include "cooler_defaults.h"   // COOLER_DEFAULT_* -- generated, see tools/import_cooler_broker.sh

struct DeviceConfig {
    std::string wifi_ssid, wifi_pass;
    std::string mqtt_host = COOLER_DEFAULT_MQTT_HOST;
    uint16_t    mqtt_port = COOLER_DEFAULT_MQTT_PORT;
    std::string mqtt_user = COOLER_DEFAULT_MQTT_USER;
    std::string mqtt_pass = COOLER_DEFAULT_MQTT_PASS;
    std::string mqtt_base = COOLER_DEFAULT_MQTT_BASE;
    bool configured = false;   // a never-configured panel is pre-filled with
                                // the cooler's own broker but still needs WiFi
};

std::string config_to_json(const DeviceConfig& c);
// Overlays parsed fields onto `out`; absent keys keep out's current value
// (a fresh DeviceConfig therefore keeps its struct defaults). Returns false on parse error.
bool config_from_json(const std::string& json, DeviceConfig& out);

struct ConfigError { bool ok; std::string message; };
ConfigError validate_config(const DeviceConfig& c);

enum class BootMode { Portal, Normal };
BootMode decide_boot_mode(bool touch_held, bool has_config, bool wifi_connected);
