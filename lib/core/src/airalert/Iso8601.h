#pragma once
#include <cstdint>
#include <cstdio>

namespace airalert {

// "2022-04-04T16:45:39.000Z" -> unix seconds UTC; 0 on failure.
// Days-from-civil per Howard Hinnant's algorithm.
inline int64_t parseIso8601Utc(const char* s) {
    if (!s) return 0;
    int y, mo, d, h, mi, se;
    if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &se) != 6) return 0;
    if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31 ||
        h > 23 || mi > 59 || se > 60) return 0;
    y -= mo <= 2;
    const int era = y / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (mo + (mo > 2 ? -3 : 9)) + 2u) / 5u + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = static_cast<int64_t>(era) * 146097 + doe - 719468;
    return days * 86400 + h * 3600 + mi * 60 + se;
}

} // namespace airalert
