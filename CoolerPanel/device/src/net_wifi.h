#pragma once
#include <string>
#include <vector>
#include "device_config.h"

// STA connect with timeout. Returns true when connected (IP obtained).
// WiFi.persistent is forced off — WiFi must never write NVS at runtime
// (flash writes during panel scan-out panic; see config_nvs.h).
bool wifi_connect(const DeviceConfig& c, uint32_t timeout_ms);

// Non-blocking variant for the boot flow (the splash stays interactive
// while connecting): start with wifi_begin, poll wifi_is_connected.
void wifi_begin(const DeviceConfig& c);
bool wifi_is_connected();

// Scan for SSIDs (for the portal dropdown). Blocking, a few seconds.
std::vector<std::string> wifi_scan();
