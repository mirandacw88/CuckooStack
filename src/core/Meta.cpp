#include "Meta.h"

#include <cstdio>
#include <ctime>
#include <sstream>

namespace cs {

// Howard Hinnant's days_from_civil / civil_from_days (proleptic Gregorian)
int64_t civilDay(const std::string& ymd) {
    int y = 0, m = 0, d = 0;
    if (ymd.size() != 10 || std::sscanf(ymd.c_str(), "%4d-%2d-%2d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 || d > 31) return kNoDay;
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = unsigned(y - era * 400);
    const unsigned doy = (153 * unsigned(m > 2 ? m - 3 : m + 9) + 2) / 5 + unsigned(d) - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + int64_t(doe) - 719468;
}

std::string civilDate(int64_t z) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = unsigned(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t y = int64_t(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02u", int(y + (m <= 2)), m, d);
    return buf;
}

uint64_t hashString(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

int64_t SystemClock::now() { return int64_t(std::time(nullptr)); }
std::string SystemClock::localDate(int64_t t) {
    const std::time_t tt = std::time_t(t);
    std::tm lt{};
#if defined(_WIN32)
    localtime_s(&lt, &tt);
#else
    localtime_r(&tt, &lt);
#endif
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
    return buf;
}
int SystemClock::localHour(int64_t t) {
    const std::time_t tt = std::time_t(t);
    std::tm lt{};
#if defined(_WIN32)
    localtime_s(&lt, &tt);
#else
    localtime_r(&tt, &lt);
#endif
    return lt.tm_hour;
}

int64_t SystemClock::utcOffset(int64_t t) {
#if defined(_WIN32)
    (void)t;
    long tz = 0;
    _get_timezone(&tz);
    return -int64_t(tz);
#else
    const std::time_t tt = std::time_t(t);
    std::tm lt{};
    localtime_r(&tt, &lt);
    return int64_t(lt.tm_gmtoff);
#endif
}

Record::Record(IStorage* storage, std::string key) : storage_(storage), key_(std::move(key)) {
    if (!storage_) return;
    const auto raw = storage_->get(key_);
    if (!raw) return;
    std::istringstream in(*raw);
    for (std::string item; std::getline(in, item, ';');) {
        const size_t eq = item.find('=');
        if (eq != std::string::npos) kv_[item.substr(0, eq)] = item.substr(eq + 1);
    }
}

int Record::i(const std::string& k, int def) const {
    const auto it = kv_.find(k);
    return it == kv_.end() || it->second.empty() ? def : std::atoi(it->second.c_str());
}
int64_t Record::l(const std::string& k, int64_t def) const {
    const auto it = kv_.find(k);
    return it == kv_.end() || it->second.empty() ? def : std::strtoll(it->second.c_str(), nullptr, 10);
}
std::string Record::s(const std::string& k, const std::string& def) const {
    const auto it = kv_.find(k);
    return it == kv_.end() ? def : it->second;
}

void Record::save() const {
    if (!storage_) return;
    std::string out;
    for (const auto& [k, v] : kv_) { out += k; out += '='; out += v; out += ';'; }
    storage_->set(key_, out);
}

} // namespace cs
