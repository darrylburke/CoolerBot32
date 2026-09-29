#pragma once
#include <stdint.h>

// Panel + touch + LVGL bring-up for the Waveshare ESP32-S3-Touch-LCD-4B.
// Must be called once before any LVGL use.
void display_init();

// Last touch state as sampled by the LVGL input driver.
bool display_touched();

// millis() of the most recent touch press (0 = never since boot).
uint32_t display_last_touch_ms();

// Boot-time gesture: true if the panel is touched within the first 300 ms
// AND held continuously for `ms`. Polls the controller directly — only call
// before the LVGL loop starts pumping the input driver.
bool display_touch_held(uint32_t ms);

// Backlight brightness 0..255 (GPIO4 PWM, active-low hardware — handled inside).
void display_set_brightness(uint8_t level);
