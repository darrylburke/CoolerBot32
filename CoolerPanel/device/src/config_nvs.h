#pragma once
#include "device_config.h"
#include "panel_config.h"

// NVS-backed DeviceConfig (namespace "coolerpanel").
//
// config_load_or_seed(): if NVS has never been written, parse the embedded
// seed (generated from the cooler YAML by tools/import_cooler_broker.sh),
// it — but WITHOUT marking `configured`, so first boot still enters the
// portal (spec §4). Otherwise load the stored config.
DeviceConfig config_load_or_seed();

// Persist every field and set configured=true (portal Save).
// FLASH-WRITE HAZARD: NVS writes disable the flash cache, and the RGB panel's
// per-vsync ISR calls flash-resident GDMA code (the prebuilt Arduino core
// lacks CONFIG_GDMA_CTRL_FUNC_IN_IRAM) -> intermittent panics. Only call this
// BEFORE display_init(). At runtime use config_request_save()/_clear(), which
// stage the change in RTC no-init RAM and reboot; config_apply_pending()
// (called first thing in setup, pre-display) performs the actual write.
void config_save(const DeviceConfig& c);

// Erase the namespace (next boot reseeds). Same hazard rule as config_save.
void config_clear();

// Runtime-safe mutations: stage in RTC RAM + ESP.restart(). Never return.
void config_request_save(const DeviceConfig& c);
void config_request_clear();

// Stage "enter the portal on next boot" + restart (no NVS involved). Used
// when setup is requested after the dashboard already owns the screen.
void config_request_portal();

// True when this boot was started by config_request_portal().
bool config_portal_boot();

// Apply a staged save/clear if one is pending. Call before display_init().
void config_apply_pending();

// Per-device random captive-portal AP password. Generated once (esp_random)
// and persisted; MUST first be called before display_init() (it writes NVS
// on first use). Survives config_clear-triggered reseeds by regenerating.
std::string config_get_ap_pass();

// Panel-local config (Task 16): alarm thresholds, backlight, night dim,
// default zoom. Belongs to the panel, not the cooler -- deliberately kept
// separate from DeviceConfig (WiFi/broker) even though it shares the same
// "coolerpanel" NVS namespace, stored whole as one JSON string under key
// "panelcfg" rather than field-by-field. panel_cfg_load() only reads, so
// (unlike config_save/_clear) it carries none of the flash-write hazard
// documented above and may be called any time before ui_init().
PanelConfig panel_cfg_load();
void panel_cfg_save(const PanelConfig& c);
