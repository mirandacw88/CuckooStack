// Reminder planning (Reminders.h): off unless enabled, one a day at most, never in quiet hours, the streak warning,
// every third day after three ignored, nothing tonight when it's too late. With a clock moved by hand.
#include "TestUtil.h"
#include "core/Reminders.h"

#include <set>

using namespace cs;
using namespace test;

int main() {
    {   // ---- reminders
        FakeClock clock; // 2026-10-08 10:00
        Tuning t;
        ReminderState st;
        check(planReminders(clock, t, st).empty(), "reminders off: nothing planned");
        st.enabled = true; st.usualHour = 23; st.streak = 5; st.playedToday = false;
        auto r = planReminders(clock, t, st);
        bool oneADay = true, quiet = true;
        std::set<int64_t> days;
        for (const Reminder& x : r) {
            oneADay &= days.insert(clock.localMidnight(x.at)).second;
            const int h = clock.localHour(x.at);
            quiet &= h >= t.notifQuietEnd && h < t.notifQuietStart;
        }
        check(!r.empty() && oneADay, "at most one reminder a day");
        check(quiet, "never in quiet hours (a 23:00 player gets theirs before 21:00)");
        check(r.front().at == clock.localMidnight(clock.now()) + t.notifStreakHour * 3600, "streak about to end: a reminder this evening");
        st.playedToday = true;
        check(planReminders(clock, t, st).front().at > clock.localMidnight(clock.now()) + 86400 - 1, "played today: no streak warning");
        st.ignored = 3;
        auto b = planReminders(clock, t, st);
        check(b.size() == 2, "3 ignored in a row: only every third day");
        clock.t = clock.localMidnight(clock.now()) + 20 * 3600; // 20:00, after the streak hour
        st.ignored = 0; st.playedToday = false;
        check(planReminders(clock, t, st).front().at >= clock.localMidnight(clock.now()) + 86400, "too late today: nothing tonight");
    }
    return finish("Reminders");
}
