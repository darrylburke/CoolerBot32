#include "timegm_compat.h"
#include <cstdint>

// UTC-only mktime: days-from-civil (Howard Hinnant's algorithm), no TZ/DST.
extern "C" time_t timegm(struct tm* tm) {
    int y = tm->tm_year + 1900;
    unsigned m = (unsigned)tm->tm_mon + 1;   // 1..12
    unsigned d = (unsigned)tm->tm_mday;      // 1..31
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);                      // [0, 399]
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; // [0, 365]
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          // [0, 146096]
    const int64_t days = (int64_t)era * 146097 + (int64_t)doe - 719468;
    return (time_t)(days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec);
}
