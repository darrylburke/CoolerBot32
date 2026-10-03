#include "platform.h"
#include <cstdlib>

// cooler_tests links cooler_shared but neither sim/main.cpp nor
// device/src/main.cpp (the two real platform_*() implementations) -- those
// are executable-specific, not part of cooler_shared. Nothing needed these
// symbols at link time until test_app_wiring.cpp started calling
// app_on_mqtt_message(), which reaches platform_epoch_utc() internally.
// A deterministic stand-in: 1700000000 unless a test moves it.
extern "C" uint32_t platform_now_ms(void) { return 0; }
// Settable for tests that need time to pass (test_platform_stub.h); every
// test that changes it puts it back.
int64_t g_test_epoch = 1700000000;
extern "C" int64_t platform_epoch_utc(void) { return g_test_epoch; }

int g_big_allocs = 0;
extern "C" void* platform_big_malloc(size_t n) { g_big_allocs++; return std::malloc(n); }
extern "C" void* platform_big_realloc(void* p, size_t n) { g_big_allocs++; return std::realloc(p, n); }
extern "C" void platform_big_free(void* p) { std::free(p); }
