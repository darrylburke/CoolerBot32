#include "platform.h"

// cooler_tests links cooler_shared but neither sim/main.cpp nor
// device/src/main.cpp (the two real platform_*() implementations) -- those
// are executable-specific, not part of cooler_shared. Nothing needed these
// symbols at link time until test_app_wiring.cpp started calling
// app_on_mqtt_message(), which reaches platform_epoch_utc() internally.
// This is a fixed, deterministic stand-in: no test asserts on the actual
// epoch/tick value, only on behaviour that's independent of it.
extern "C" uint32_t platform_now_ms(void) { return 0; }
extern "C" int64_t platform_epoch_utc(void) { return 1700000000; }
