#include "backlight_policy.h"

uint8_t backlight_level(uint8_t user, bool night, uint32_t idle_ms, bool alarm) {
    if (alarm) return user;
    if (!night) return idle_ms < kIdleDimMs ? user : 0;   // idle by day: off
    if (idle_ms < kNightWakeMs) return user;
    return user < kBacklightDim ? user : kBacklightDim;
}
