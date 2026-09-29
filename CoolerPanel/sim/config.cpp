#include "config.h"
#include <ArduinoJson.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <cstdio>

SimConfig config_load(int argc, char** argv) {
    SimConfig c;
    const char* path = getenv("LLMMON_CONFIG");
    std::string p = path ? path : "llmmon_config.json";
    std::ifstream f(p);
    if (f) {
        std::stringstream ss; ss << f.rdbuf();
        JsonDocument d;
        if (!deserializeJson(d, ss.str())) {
            auto mq = d["mqtt"];
            c.host = mq["host"] | c.host;
            c.port = mq["port"] | c.port;
            c.user = mq["username"] | c.user;
            c.pass = mq["password"] | c.pass;
        }
    }
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host") && i+1<argc) c.host = argv[++i];
        else if (!strcmp(argv[i], "--port") && i+1<argc) c.port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--user") && i+1<argc) c.user = argv[++i];
        else if (!strcmp(argv[i], "--pass") && i+1<argc) c.pass = argv[++i];
        else if (!strcmp(argv[i], "--base") && i+1<argc) c.base = argv[++i];
    }
    return c;
}

// ---- Panel-local config (Task 16) -------------------------------------------
// Simulator analogue of device/src/config_nvs.cpp's NVS-backed
// panel_cfg_load()/panel_cfg_save(): same JSON shape, just a plain file
// instead of a Preferences namespace, so simulator runs keep alarm/
// backlight/zoom settings across restarts too.
static std::string panel_cfg_path() {
    const char* home = getenv("HOME");
    return std::string(home ? home : ".") + "/.cooler_panel.json";
}

PanelConfig panel_cfg_load() {
    PanelConfig c;   // struct defaults; also the fallback if the file is
                      // missing or fails to parse (panel_config_from_json
                      // leaves `c` untouched on a parse error)
    std::ifstream f(panel_cfg_path());
    if (f) {
        std::stringstream ss; ss << f.rdbuf();
        std::string body = ss.str();
        if (!panel_config_from_json(body.c_str(), c))
            fprintf(stderr, "config: %s failed to parse, using defaults\n",
                    panel_cfg_path().c_str());
    }
    return c;
}

void panel_cfg_save(const PanelConfig& c) {
    std::ofstream f(panel_cfg_path());
    if (f) f << panel_config_to_json(c);
}

// Persist a DeviceConfig edited on-panel. Mirrors the device's NVS write so
// the simulator exercises the same Save path rather than a stub that always
// succeeds.
#include "device_config.h"
bool sim_save_device_config(const DeviceConfig& c) {
    const char* home = getenv("HOME");
    if (!home) return false;
    std::string path = std::string(home) + "/.cooler_panel_device.json";
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return false;
    std::string j = config_to_json(c);
    bool ok = fwrite(j.data(), 1, j.size(), f) == j.size();
    fclose(f);
    fprintf(stderr, "config saved to %s\n", path.c_str());
    return ok;
}
