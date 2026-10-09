// Reminder planning (13+ players who turned reminders on). Platform-free: the game rebuilds the whole set whenever
// something changes (run end, going to the background) and hands it to INotifications::replaceAll.
//
// Rules: at most one a day; never in quiet hours (Tuning notifQuietStart..notifQuietEnd); daily ones land at the
// hour the player usually plays; after 3 ignored in a row, only every third day. Never for children.
#pragma once

#include "Services.h"
#include "Tuning.h"

#include <vector>

namespace cs {

struct ReminderState {
    bool enabled = false;        // 13+ and the player said yes
    int usualHour = 19;          // Profile::usualHour
    int streak = 0;              // current daily streak
    bool playedToday = false;
    int dropStep = 0;            // next daily-drop ladder step (0..6)
    int ignored = 0;             // reminders in a row that didn't bring the player back
};

std::vector<Reminder> planReminders(IClock& clock, const Tuning& t, const ReminderState& s, int days = 7);

} // namespace cs
