#include "net_wifi.h"
#include <Arduino.h>
#include <WiFi.h>

void wifi_begin(const DeviceConfig& c) {
    WiFi.persistent(false);        // no NVS writes at runtime (config_nvs.h)
    WiFi.mode(WIFI_STA);
    WiFi.begin(c.wifi_ssid.c_str(), c.wifi_pass.c_str());
}

bool wifi_is_connected() { return WiFi.status() == WL_CONNECTED; }

bool wifi_connect(const DeviceConfig& c, uint32_t timeout_ms) {
    wifi_begin(c);
    uint32_t t0 = millis();
    while (millis() - t0 < timeout_ms) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("wifi: connected, ip=%s rssi=%d\n",
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
            return true;
        }
        delay(100);
    }
    Serial.printf("wifi: connect to '%s' timed out after %lu ms\n",
                  c.wifi_ssid.c_str(), (unsigned long)timeout_ms);
    return false;
}

std::vector<std::string> wifi_scan() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);        // keep a portal AP alive while scanning
    int n = WiFi.scanNetworks();
    std::vector<std::string> out;
    for (int i = 0; i < n; i++) {
        std::string ssid = WiFi.SSID(i).c_str();
        if (ssid.empty()) continue;
        bool dup = false;
        for (auto& s : out) if (s == ssid) { dup = true; break; }
        if (!dup) out.push_back(ssid);
    }
    WiFi.scanDelete();
    return out;
}
