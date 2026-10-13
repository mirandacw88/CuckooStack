// The platform service interfaces (Services.h): the calendar helpers every platform clock shares, today() and
// localMidnight(), across time zones, at midnight itself and before the epoch.
#include "TestUtil.h"
#include "core/Meta.h"

using namespace cs;
using namespace test;

namespace {

// a clock in a fixed time zone (seconds east of UTC)
struct ZoneClock : IClock {
    int64_t t = 0, offset = 0;
    int64_t now() override { return t; }
    int64_t utcOffset(int64_t) override { return offset; }
    std::string localDate(int64_t s) override { const int64_t l = s + offset; return civilDate(l >= 0 ? l / 86400 : (l - 86399) / 86400); }
    int localHour(int64_t s) override { return int(((s + offset) % 86400 + 86400) % 86400 / 3600); }
};

void midnight() {
    ZoneClock c;
    const int64_t day = civilDay("2026-10-13") * 86400; // 00:00 UTC
    c.t = day + 15 * 3600;
    check(c.localMidnight(c.t) == day && c.today() == "2026-10-13", "UTC: midnight of today");
    check(c.localMidnight(day) == day, "exactly at midnight: that day");
    check(c.localMidnight(day - 1) == day - 86400, "one second before midnight: the day before");

    c.offset = -7 * 3600; // UTC-7: 15:00 UTC is 08:00 local, same date
    check(c.localMidnight(c.t) == day + 7 * 3600 && c.localHour(c.localMidnight(c.t)) == 0, "UTC-7: local midnight is 07:00 UTC");
    c.t = day + 3 * 3600; // 03:00 UTC = 20:00 the day before, local
    check(c.today() == "2026-10-12" && c.localMidnight(c.t) == day - 86400 + 7 * 3600, "UTC-7: still yesterday locally at 03:00 UTC");

    c.offset = 9 * 3600; // UTC+9: 20:00 UTC is 05:00 the next day, local
    c.t = day + 20 * 3600;
    check(c.today() == "2026-10-14" && c.localMidnight(c.t) == day + 86400 - 9 * 3600, "UTC+9: already tomorrow locally");
    check(c.localHour(c.localMidnight(c.t)) == 0 && c.localDate(c.localMidnight(c.t)) == c.today(), "the midnight found is 00:00 of today's date");

    c.offset = 5 * 3600 + 1800; // half-hour zones (India)
    c.t = day + 19 * 3600;
    check(c.localHour(c.localMidnight(c.t)) == 0 && c.localMidnight(c.t) <= c.t && c.t - c.localMidnight(c.t) < 86400, "UTC+5:30: a half-hour zone");

    c.offset = 0;
    c.t = -3600; // 1969-12-31 23:00 UTC
    check(c.localMidnight(c.t) == -86400 && c.today() == "1969-12-31", "before the epoch: rounds down, not toward zero");
}

void sameDay() {
    ZoneClock c;
    c.offset = -5 * 3600;
    const int64_t m = c.localMidnight(civilDay("2026-03-08") * 86400 + 12 * 3600);
    bool stable = true;
    for (int64_t s = m; s < m + 86400; s += 997) stable = stable && c.localMidnight(s) == m;
    check(stable, "every second of a local day maps to the same midnight");
}

} // namespace

int main() {
    midnight();
    sameDay();
    return finish("Services");
}
