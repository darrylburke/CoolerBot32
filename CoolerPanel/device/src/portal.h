#pragma once
#include <string>
#include "device_config.h"

// Captive-portal setup mode: SoftAP + wildcard DNS + web config form + LCD
// info screen. Never returns — Save/Reset stage the config change and reboot.
// ap_pass comes from config_get_ap_pass() (fetched pre-display by main).
void portal_run(const DeviceConfig& current, const std::string& ap_pass);
