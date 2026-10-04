#pragma once
#include <stdint.h>

// Night level, 0..255 (~5%): the night-dim window dims to this 30 s after the
// last touch. An LED backlight looks far brighter than its duty cycle; 20%
// (50) read as barely dimmed on the panel.
constexpr uint8_t kBacklightDim = 13;
// By day the backlight goes off after kIdleDimMs without a touch or button
// press.
constexpr uint32_t kIdleDimMs = 30u * 60u * 1000u;
constexpr uint32_t kNightWakeMs = 30u * 1000u;

// Backlight level, 0..255 (0 = off). `user` is the configured daytime level,
// `night` whether the night-dim window applies now, `idle_ms` the time since
// the last touch or button press (boot counts as one), `alarm` whether an
// unacknowledged alarm is showing -- which always keeps the screen at `user`.
// Night dimming never raises a level the user already set below kBacklightDim.
uint8_t backlight_level(uint8_t user, bool night, uint32_t idle_ms, bool alarm);
