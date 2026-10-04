#pragma once
#include <stdint.h>

// Dimmed level, 0..255 (~20%): used at night and after a long idle spell.
constexpr uint8_t kBacklightDim = 50;
// How long the screen stays at the user level after the last touch or button
// press: 30 min by day, 30 s inside the night-dim window.
constexpr uint32_t kIdleDimMs = 30u * 60u * 1000u;
constexpr uint32_t kNightWakeMs = 30u * 1000u;

// Brightness to show, 0..255. `user` is the configured daytime level,
// `night` whether the night-dim window applies now, `idle_ms` the time since
// the last touch or button press (boot counts as one), `alarm` whether an
// unacknowledged alarm is showing -- which always keeps the screen bright.
// Dimming never raises a level the user already set below kBacklightDim.
uint8_t backlight_level(uint8_t user, bool night, uint32_t idle_ms, bool alarm);
