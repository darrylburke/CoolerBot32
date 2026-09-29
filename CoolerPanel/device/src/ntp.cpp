#include "ntp.h"
#include <Arduino.h>

// POSIX TZ string for local-time features (night dimming). Override with
// -DLLMMON_TZ='"..."' in platformio.ini. time(nullptr) stays UTC either way.
#ifndef LLMMON_TZ
#define LLMMON_TZ "EST5EDT,M3.2.0,M11.1.0"   // America/Toronto
#endif

void ntp_begin() {
    configTzTime(LLMMON_TZ, "pool.ntp.org");
}
