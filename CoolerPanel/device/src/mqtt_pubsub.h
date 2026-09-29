#pragma once
#include "device_config.h"

// MQTT transport feeding the shared AppModel (app_on_mqtt_message /
// app_set_link_state). Single-threaded: mqtt_poll() must be pumped from the
// main loop, same model as the sim's libmosquitto transport.
void mqtt_begin(const DeviceConfig& c);
void mqtt_poll();
