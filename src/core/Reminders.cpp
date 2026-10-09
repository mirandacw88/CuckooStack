#include "Reminders.h"

#include <algorithm>

namespace cs {

std::vector<Reminder> planReminders(IClock& clock, const Tuning& t, const ReminderState& s, int days) {
    std::vector<Reminder> out;
    if (!s.enabled) return out;
    const int64_t now = clock.now();
    const int64_t today = clock.localMidnight(now);
    const int lo = std::clamp(t.notifQuietEnd, 0, 23), hi = std::clamp(t.notifQuietStart, lo + 1, 24); // allowed [lo, hi)
    auto at = [&](int dayOffset, int hour) { return today + int64_t(dayOffset) * 86400 + int64_t(std::clamp(hour, lo, hi - 1)) * 3600; };
    const bool backoff = s.ignored >= 3;

    // today: the streak is about to end (only if there's something to lose and time to act)
    if (!backoff && s.streak >= 2 && !s.playedToday) {
        const int64_t when = at(0, t.notifStreakHour);
        if (when > now + 30 * 60)
            out.push_back({100, when, "Your " + std::to_string(s.streak) + "-day streak ends tonight",
                           "One quick run keeps the flame alive."});
    }
    // the next days: a new course every day, at the player's usual time
    for (int d = 1; d < days; ++d) {
        if (backoff && d % 3 != 0) continue;
        const int64_t when = at(d, s.usualHour);
        const int step = (s.dropStep + d - 1) % 7;
        const int variant = int((today / 86400 + d) % 4);
        Reminder r{100 + d, when, "", ""};
        if (d == 1 && s.streak >= 2) {
            r.title = "Day " + std::to_string(s.streak + 1) + " of your streak";
            r.body = "A new course just dropped. Keep the flame going!";
        } else if (variant == 0) {
            r.title = "New course is live";
            r.body = "Same course for everyone today. Can you top the board?";
        } else if (variant == 1) {
            r.title = step == 6 ? "Day 7 chest is waiting" : "Your daily drop is ready";
            r.body = step == 6 ? "The big one: a chest full of coins." : "Free coins are waiting in today's drop.";
        } else if (variant == 2) {
            r.title = "Three new missions";
            r.body = "Fresh jobs, fresh coins. Jack in?";
        } else {
            r.title = "The hen is getting restless";
            r.body = "One run. Just one. Probably.";
        }
        out.push_back(std::move(r));
    }
    return out;
}

} // namespace cs
