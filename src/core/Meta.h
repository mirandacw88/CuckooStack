// Shared helpers for the meta-game modules (Economy, Monetization, Daily): calendar maths on "YYYY-MM-DD" strings
// and small key=value records persisted under one storage key each.
#pragma once

#include "Services.h"

#include <cstdint>
#include <map>
#include <string>

namespace cs {

constexpr int64_t kNoDay = INT64_MIN;
int64_t civilDay(const std::string& ymd);   // days since 1970-01-01, kNoDay if malformed
std::string civilDate(int64_t day);         // inverse of civilDay
uint64_t hashString(const std::string& s);  // FNV-1a, for deterministic per-day picks

// One saved record: "k=v;k=v". Keys and values must not contain ';' or '='.
class Record {
public:
    Record(IStorage* storage, std::string key);
    int i(const std::string& k, int def = 0) const;
    int64_t l(const std::string& k, int64_t def = 0) const;
    std::string s(const std::string& k, const std::string& def = {}) const;
    bool has(const std::string& k) const { return kv_.count(k) != 0; }
    void set(const std::string& k, int64_t v) { kv_[k] = std::to_string(v); }
    void set(const std::string& k, const std::string& v) { kv_[k] = v; }
    void erase(const std::string& k) { kv_.erase(k); }
    void save() const;
    void clear() { kv_.clear(); }

private:
    IStorage* storage_;
    std::string key_;
    std::map<std::string, std::string> kv_;
};

} // namespace cs
