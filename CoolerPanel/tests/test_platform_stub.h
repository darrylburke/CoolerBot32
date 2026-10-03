#pragma once
#include <stdint.h>

// What platform_epoch_utc() returns under test (default 1700000000).
extern int64_t g_test_epoch;

// Calls to platform_big_malloc()/platform_big_realloc() since last reset.
extern int g_big_allocs;
