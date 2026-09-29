#pragma once
// Night dimming (spec §8 / display_design §5.4): ~20% brightness 23:00–07:00
// local time, the user level during the day, and back to the user level for
// 30 s after any touch. Requires display_init() (PWM + touch) and an LVGL
// timer pump.
void backlight_init();

// Cycle the user brightness level: 100% -> 50% -> 15% -> 100% (physical
// button). Applies immediately and becomes the daytime level.
void backlight_cycle();

// Task 14: apply the panel-local backlight config (Task 16's persisted,
// already-clamped-to-5..100 PanelConfig::backlight/night_dim) as the
// daytime level and the night-dimming on/off switch. Call once at startup,
// after panel_cfg_load() and after backlight_init() has brought the PWM
// channel up -- overrides backlight_init()'s built-in default, and any
// later physical-button press (backlight_cycle()) overrides this in turn.
void backlight_apply_config(int percent, bool night_dim);
