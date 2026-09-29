#pragma once
#include <string>
#include "panel_config.h"
#include "cooler_defaults.h"

// Defaults are the cooler's broker and the panel's own login, generated from
// the controller project by tools/import_cooler_broker.sh -- the same defaults
// the device firmware boots with, so `./build/cooler_sim` with no arguments
// talks to the real controller exactly as the panel does. Port 8883 means TLS,
// verified against the compiled-in CA (COOLER_DEFAULT_MQTT_CA).
struct SimConfig {
    std::string host = COOLER_DEFAULT_MQTT_HOST;
    int         port = COOLER_DEFAULT_MQTT_PORT;
    std::string user = COOLER_DEFAULT_MQTT_USER;
    std::string pass = COOLER_DEFAULT_MQTT_PASS;
    std::string base = COOLER_DEFAULT_MQTT_BASE;   // topic tree: <base>/data, /availability, /cmd
};
SimConfig config_load(int argc, char** argv);

// Panel-local config (Task 16): alarm thresholds, backlight, night dim,
// default zoom. Deliberately separate from SimConfig (broker connection) --
// PanelConfig is never sent to the cooler. Persisted to ~/.cooler_panel.json
// so simulator runs keep their settings across restarts, the sim analogue of
// device/src/config_nvs.cpp's NVS-backed panel_cfg_load()/panel_cfg_save().
PanelConfig panel_cfg_load();
void panel_cfg_save(const PanelConfig& c);
