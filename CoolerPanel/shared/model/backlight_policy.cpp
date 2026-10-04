#include "backlight_policy.h"

uint8_t backlight_level(uint8_t user, bool night, uint32_t idle_ms, bool alarm) {
    if (alarm) return user;
    const uint32_t awake_ms = night ? kNightWakeMs : kIdleDimMs;
    if (idle_ms < awake_ms) return user;
    return user < kBacklightDim ? user : kBacklightDim;
}
