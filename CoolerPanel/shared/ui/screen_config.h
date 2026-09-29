#pragma once
#include "lvgl.h"
struct DeviceConfig;

// On-panel WiFi / MQTT value-entry form.
//
// This is deliberately NOT screen_setup.cpp. That one shows the panel's own
// access-point name, password and join-QR so you can provision it from a
// phone browser -- useful on a virgin device, useless once the panel is on
// the wall and you just want to change the broker. This screen lets you type
// the values directly on the panel.
//
// It is created hidden, over the tileview, and shown from the Detail page.
lv_obj_t* screen_config_create(lv_obj_t* parent);
void screen_config_open(void);      // populate from the live config and show
void screen_config_close(void);
bool screen_config_is_open(void);

// Persisting a config is platform work (NVS on the device, a JSON file in the
// simulator), so shared/ takes a callback rather than reaching for either.
// Return false to keep the form open and show the failure.
typedef bool (*config_save_fn)(const DeviceConfig&);
void screen_config_set_saver(config_save_fn fn);
