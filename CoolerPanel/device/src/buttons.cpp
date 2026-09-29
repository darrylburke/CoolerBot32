#include "buttons.h"
#include <Arduino.h>
#include <Wire.h>
#include "backlight.h"

// TCA9554 expander @0x20: input port reg 0, Key3 (the side button) on P4.
// The button is also wired to the AXP2101 power key; only this source is
// handled so a press steps exactly once (see buttons.h).
static constexpr uint8_t kExpAddr = 0x20;
static constexpr uint8_t kKey3Mask = 0x10;

static int s_key3_idle = -1;      // input level with the key released
static bool s_key3_down = false;
static bool s_key3_raw_prev = false;
static uint32_t s_accepted_ms = 0;

static constexpr uint32_t kRefractoryMs = 350;   // min gap between presses

static int exp_input() {
    Wire.beginTransmission(kExpAddr);
    Wire.write(0);
    if (Wire.endTransmission(false) != 0) return -1;
    if (Wire.requestFrom((int)kExpAddr, 1) != 1) return -1;
    return Wire.read();
}

void buttons_init() {
    int in = exp_input();
    if (in >= 0) s_key3_idle = in & kKey3Mask;
}

void buttons_poll() {
    static uint32_t last = 0;
    if (millis() - last < 50) return;               // 20 Hz is plenty
    last = millis();
    if (s_key3_idle < 0) return;
    int in = exp_input();
    if (in < 0) return;
    bool raw = (in & kKey3Mask) != s_key3_idle;     // level away from idle

    // Debounce: accept a press only when two consecutive 50 ms samples agree
    // (contact bounce shows as single-sample blips), and enforce a 350 ms
    // refractory between accepted presses.
    bool down = raw && s_key3_raw_prev;
    s_key3_raw_prev = raw;
    if (down && !s_key3_down && millis() - s_accepted_ms >= kRefractoryMs) {
        s_accepted_ms = millis();
        Serial.println("button: side key -> cycle brightness");
        backlight_cycle();
    }
    if (down || !raw) s_key3_down = down;
}
