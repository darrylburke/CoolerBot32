#pragma once
#include <stdint.h>
#include <stddef.h>
const char* fmt_tokens(int64_t n, char* out, size_t cap);
const char* fmt_cost(double usd, char* out, size_t cap);
const char* fmt_age(int64_t seconds, char* out, size_t cap);
