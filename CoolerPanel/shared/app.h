#pragma once
#include "platform.h"

struct CoolerState;
CoolerState& cooler_state();

class History;
History& panel_history();

class Commands;
Commands& panel_commands();

class Alarms;
Alarms& panel_alarms();

struct PanelConfig;
// Panel-local settings (alarm thresholds, backlight, night dim, default
// zoom) -- belongs to the panel, never published to the cooler. Callers
// that need the load/save side effects link against config_nvs.h (device)
// or sim/config.h (sim); this accessor just holds the in-memory value.
PanelConfig& panel_config();

// 7 days at 30 s (falls back to 24 h if the big allocation fails). Call once
// from ui_init() before the trend screen's first refresh.
void app_init_history();

#ifdef __cplusplus
extern "C" {
#endif
void app_on_mqtt_message(const char* topic, const uint8_t* payload, size_t len);
void app_set_link_state(link_state_t s);
link_state_t app_link_state(void);
#ifdef __cplusplus
}
#endif
