#include "config_nvs.h"
#include <Arduino.h>
#include <Preferences.h>
#include "assets_gen/config_seed.h"   // kConfigSeedJson (gitignored, real creds)

static const char* kNs = "coolerpanel";  // NOT "llmmon": a board with a prior
                                              // LLMMon install must not inherit its
                                              // config (and skip setup entirely).

static void write_fields(Preferences& p, const DeviceConfig& c) {
    p.putString("ssid",  c.wifi_ssid.c_str());
    p.putString("wpass", c.wifi_pass.c_str());
    p.putString("host",  c.mqtt_host.c_str());
    p.putUShort("port",  c.mqtt_port);
    p.putString("muser", c.mqtt_user.c_str());
    p.putString("mpass", c.mqtt_pass.c_str());
    p.putString("base",  c.mqtt_base.c_str());
}

DeviceConfig config_load_or_seed() {
    Preferences p;
    p.begin(kNs, false);
    DeviceConfig c;
    if (!p.isKey("cfg")) {
        // Never written: seed from the embedded config (the struct's own
        // COOLER_DEFAULT_* members already point at the cooler's broker --
        // config_from_json only overlays whatever the seed JSON provides).
        if (!config_from_json(kConfigSeedJson, c))
            Serial.println("config: embedded seed failed to parse!");
        c.configured = false;          // seed is usable but device is unconfigured
        write_fields(p, c);
        p.putBool("cfg", false);
        p.putUChar("bver", COOLER_BROKER_VERSION);
        Serial.println("config: NVS seeded from embedded defaults");
    } else {
        c.wifi_ssid = p.getString("ssid", "").c_str();
        c.wifi_pass = p.getString("wpass", "").c_str();
        c.mqtt_host = p.getString("host", c.mqtt_host.c_str()).c_str();
        c.mqtt_port = p.getUShort("port", c.mqtt_port);
        c.mqtt_user = p.getString("muser", c.mqtt_user.c_str()).c_str();
        c.mqtt_pass = p.getString("mpass", c.mqtt_pass.c_str()).c_str();
        c.mqtt_base = p.getString("base", c.mqtt_base.c_str()).c_str();
        c.configured = p.getBool("cfg", false);

        // One-time broker migration. Settings saved before the current broker
        // (COOLER_BROKER_VERSION in cooler_defaults.h) point at a retired server
        // and login: reset the MQTT fields to the compiled-in defaults, keep the
        // WiFi and the "configured" flag, so the panel reconnects without a
        // visit to the setup portal. Runs before display_init(), like every
        // other NVS write here.
        if (p.getUChar("bver", 1) < COOLER_BROKER_VERSION) {
            const DeviceConfig d;          // struct defaults = COOLER_DEFAULT_*
            Serial.printf("config: migrating MQTT %s:%u -> %s:%u\n",
                          c.mqtt_host.c_str(), c.mqtt_port,
                          d.mqtt_host.c_str(), d.mqtt_port);
            c.mqtt_host = d.mqtt_host; c.mqtt_port = d.mqtt_port;
            c.mqtt_user = d.mqtt_user; c.mqtt_pass = d.mqtt_pass;
            c.mqtt_base = d.mqtt_base;
            write_fields(p, c);
            p.putUChar("bver", COOLER_BROKER_VERSION);
        }
    }
    p.end();
    return c;
}

void config_save(const DeviceConfig& c) {
    Preferences p;
    p.begin(kNs, false);
    write_fields(p, c);
    p.putBool("cfg", true);
    p.putUChar("bver", COOLER_BROKER_VERSION);   // a hand-set broker is never migrated
    p.end();
}

void config_clear() {
    Preferences p;
    p.begin(kNs, false);
    p.clear();
    p.end();
}

// ---- Panel-local config (Task 16) -------------------------------------------
// Stored whole, as one JSON string, under key "panelcfg" in the same
// namespace DeviceConfig uses -- unlike DeviceConfig's field-by-field
// layout, there's no per-field NVS size/type pressure here (the whole blob
// is well under a hundred bytes), so round-tripping through
// panel_config_to_json/panel_config_from_json keeps this in one place
// instead of duplicating every field name twice.
PanelConfig panel_cfg_load() {
    Preferences p;
    p.begin(kNs, false);
    String j = p.getString("panelcfg", "");
    p.end();
    PanelConfig c;   // struct defaults; also the fallback on parse failure
                      // (panel_config_from_json leaves `c` untouched then)
    if (j.length() && !panel_config_from_json(j.c_str(), c))
        Serial.println("config: panelcfg NVS entry failed to parse, using defaults");
    return c;
}

void panel_cfg_save(const PanelConfig& c) {
    Preferences p;
    p.begin(kNs, false);
    p.putString("panelcfg", panel_config_to_json(c).c_str());
    p.end();
}

std::string config_get_ap_pass() {
    Preferences p;
    p.begin(kNs, false);
    String pass = p.getString("appass", "");
    if (pass.length() == 0) {
        // Unambiguous alphabet (no 0/O/1/l), 10 chars, hardware RNG.
        static const char kAlpha[] = "abcdefghjkmnpqrstuvwxyz23456789ABCDEFGHJKMNPQRSTUVWXYZ";
        char buf[11];
        for (int i = 0; i < 10; i++)
            buf[i] = kAlpha[esp_random() % (sizeof(kAlpha) - 1)];
        buf[10] = '\0';
        pass = buf;
        p.putString("appass", pass);
        Serial.println("config: generated new AP password");
    }
    p.end();
    return std::string(pass.c_str());
}

// ---- Runtime-safe staged writes (see header) --------------------------------
// RTC no-init RAM survives ESP.restart() but not power loss — fine for a
// "reboot immediately and apply" handoff.
struct PendingConfig {
    uint32_t magic;              // kMagicSave / kMagicClear
    char ssid[64], wpass[64];
    char host[128], muser[64], mpass[64], base[32];
    uint16_t port;
};
static constexpr uint32_t kMagicSave   = 0x4C4C4D53;   // "LLMS"
static constexpr uint32_t kMagicClear  = 0x4C4C4D43;   // "LLMC"
static constexpr uint32_t kMagicPortal = 0x4C4C4D50;   // "LLMP"
RTC_NOINIT_ATTR static PendingConfig s_pending;
static bool s_portal_boot = false;

static void copy_str(char* dst, size_t n, const std::string& s) {
    strlcpy(dst, s.c_str(), n);
}

void config_request_save(const DeviceConfig& c) {
    memset(&s_pending, 0, sizeof s_pending);
    copy_str(s_pending.ssid,  sizeof s_pending.ssid,  c.wifi_ssid);
    copy_str(s_pending.wpass, sizeof s_pending.wpass, c.wifi_pass);
    copy_str(s_pending.host,  sizeof s_pending.host,  c.mqtt_host);
    copy_str(s_pending.muser, sizeof s_pending.muser, c.mqtt_user);
    copy_str(s_pending.mpass, sizeof s_pending.mpass, c.mqtt_pass);
    copy_str(s_pending.base,  sizeof s_pending.base,  c.mqtt_base);
    s_pending.port = c.mqtt_port;
    s_pending.magic = kMagicSave;
    ESP.restart();
}

void config_request_clear() {
    memset(&s_pending, 0, sizeof s_pending);
    s_pending.magic = kMagicClear;
    ESP.restart();
}

void config_request_portal() {
    memset(&s_pending, 0, sizeof s_pending);
    s_pending.magic = kMagicPortal;
    ESP.restart();
}

bool config_portal_boot() { return s_portal_boot; }

void config_apply_pending() {
    if (s_pending.magic == kMagicSave) {
        DeviceConfig c;
        c.wifi_ssid = s_pending.ssid;  c.wifi_pass = s_pending.wpass;
        c.mqtt_host = s_pending.host;  c.mqtt_user = s_pending.muser;
        c.mqtt_pass = s_pending.mpass; c.mqtt_base = s_pending.base;
        c.mqtt_port = s_pending.port;
        c.configured = true;
        config_save(c);
        Serial.println("config: applied staged save");
    } else if (s_pending.magic == kMagicClear) {
        config_clear();
        Serial.println("config: applied staged clear");
    } else if (s_pending.magic == kMagicPortal) {
        s_portal_boot = true;
        Serial.println("config: portal requested for this boot");
    }
    s_pending.magic = 0;
}
