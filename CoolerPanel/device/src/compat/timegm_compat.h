/* Force-included into every C++ compile via extra_script_compat.py: ESP32
 * newlib has no timegm(), which shared/model/mqtt_router.cpp (frozen) calls.
 * Declaration here; implementation in timegm_compat.cpp. */
#pragma once
#include <time.h>
#ifdef __cplusplus
extern "C"
#endif
time_t timegm(struct tm* tm);
