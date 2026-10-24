// In-memory services for a game nobody sees: the level checker's and the level editors' bot runs (BotRun.h). Storage
// that already holds a 13+ player who has answered the age screen and claimed today's drop (so no screen pops up over
// the title), and a clock fixed on one morning.
#pragma once

#include "Meta.h"
#include "Services.h"

#include <map>
#include <optional>
#include <string>

namespace cs {

struct MemoryStorage : IStorage {
    std::map<std::string, std::string> kv;
    std::optional<std::string> get(const std::string& k) override { auto it = kv.find(k); if (it == kv.end()) return std::nullopt; return it->second; }
    void set(const std::string& k, const std::string& v) override { kv[k] = v; }
    static MemoryStorage ready() {
        MemoryStorage s;
        s.kv["cluckstack-profile"] = "aud=2;";
        s.kv["cluckstack-drop"] = "claims=1;last=2026-10-08;";
        return s;
    }
};

// 2026-10-08 10:00 local (the epoch taken as local time)
struct FixedClock : IClock {
    int64_t t = civilDay("2026-10-08") * 86400 + 10 * 3600;
    int64_t now() override { return t; }
    std::string localDate(int64_t s) override { return civilDate(s >= 0 ? s / 86400 : (s - 86399) / 86400); }
    int localHour(int64_t s) override { return int(((s % 86400) + 86400) % 86400 / 3600); }
};

} // namespace cs
