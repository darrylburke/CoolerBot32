#include "format.h"
#include <cstdio>
#include <cstring>

static const char* EMDASH = "—";

static void scaled(int64_t n, double div, char unit, char* out, size_t cap) {
    double v = (double)n / div;
    // If rounding to 1 decimal would reach 100 (6 chars, e.g. "100.0M"),
    // drop the decimal to keep the 5-char cap.
    double rounded1 = (double)((long long)(v * 10.0 + 0.5)) / 10.0;
    if (rounded1 < 100.0) snprintf(out, cap, "%.1f%c", v, unit);
    else                  snprintf(out, cap, "%.0f%c", v, unit);
}

const char* fmt_tokens(int64_t n, char* out, size_t cap) {
    if (n < 0) { snprintf(out, cap, "%s", EMDASH); return out; }
    if (n < 1000)            snprintf(out, cap, "%lld", (long long)n);
    else if (n < 1000000)    scaled(n, 1e3, 'K', out, cap);
    else if (n < 1000000000) scaled(n, 1e6, 'M', out, cap);
    else                     scaled(n, 1e9, 'B', out, cap);
    return out;
}

const char* fmt_cost(double usd, char* out, size_t cap) {
    if (usd < 0) { snprintf(out, cap, "%s", EMDASH); return out; }
    double cents_rounded = (double)((long long)(usd * 100.0 + 0.5)) / 100.0;
    if (cents_rounded >= 100.0) snprintf(out, cap, "$%.0f", cents_rounded);
    else                        snprintf(out, cap, "$%.2f", usd);
    return out;
}

const char* fmt_age(int64_t s, char* out, size_t cap) {
    if (s < 0)        { snprintf(out, cap, "%s", EMDASH); return out; }
    if (s < 60)       snprintf(out, cap, "%llds", (long long)s);
    else if (s < 3600)  snprintf(out, cap, "%lldm", (long long)(s / 60));
    else if (s < 86400) snprintf(out, cap, "%lldh", (long long)(s / 3600));
    else                snprintf(out, cap, "%lldd", (long long)(s / 86400));
    return out;
}
