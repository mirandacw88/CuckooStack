// Streak, missions, daily drop, level and the age screen (Daily.h), with a clock moved by hand.
#include "TestUtil.h"
#include "core/Reminders.h"

#include <set>

using namespace cs;
using namespace test;

int main() {
    {   // ---- calendar maths
        check(civilDate(civilDay("2026-10-08")) == "2026-10-08", "civilDay round-trips");
        check(civilDay("2026-03-01") - civilDay("2026-02-28") == 1, "month boundary");
        check(civilDay("2024-03-01") - civilDay("2024-02-28") == 2, "leap year");
        check(civilDay("bad") == kNoDay, "malformed date");
    }
    {   // ---- streak
        FakeStorage st;
        FakeClock clock;
        Streak s(&st, &clock);
        check(s.status() == Streak::Status::Fresh && s.count() == 0, "fresh");
        s.played(); s.played();
        check(s.count() == 1, "day 1 (twice on the same day counts once)");
        clock.nextDay(); s.played();
        check(s.count() == 2, "day 2");
        clock.nextDay(); clock.nextDay();
        check(s.status() == Streak::Status::Saveable && s.count() == 2, "missed one day: saveable, count still shown");
        s.keep(); s.played();
        check(s.count() == 3, "kept: the streak continues");
        clock.nextDay(); clock.nextDay(); clock.nextDay();
        check(s.status() == Streak::Status::Broken && s.count() == 0, "missed two days: broken");
        s.played();
        check(s.count() == 1 && s.best() == 3, "starts again at 1, best remembered");
    }
    {   // ---- missions
        FakeStorage a, b;
        FakeClock clock;
        Missions m1(&a, &clock), m2(&b, &clock);
        bool same = true;
        for (int i = 0; i < 3; ++i) same &= m1.list()[size_t(i)].kind == m2.list()[size_t(i)].kind && m1.list()[size_t(i)].target == m2.list()[size_t(i)].target;
        check(same, "every player gets the same three missions today");
        check(m1.list()[0].kind != m1.list()[1].kind && m1.list()[1].kind != m1.list()[2].kind && m1.list()[0].kind != m1.list()[2].kind, "three different kinds");
        const Mission first = m1.list()[0];
        int done = 0;
        for (int i = 0; i < 500 && !(done & 1); ++i)
            done |= first.kind == MissionKind::Distance ? m1.recordDistance(10 * i) : m1.record(first.kind, 1);
        check((done & 1) && m1.list()[0].done, "progress completes a mission (bit reported once)");
        const MissionKind before = m1.list()[1].kind;
        m1.reroll(1);
        check(m1.list()[1].kind != before && m1.rerolls() == 1, "reroll swaps the mission");
        Missions m3(&a, &clock);
        check(m3.list()[0].done && m3.list()[1].kind == m1.list()[1].kind, "missions persist");
        clock.nextDay();
        m3.refresh();
        check(!m3.list()[0].done && m3.rerolls() == 0, "a new day brings new missions");
    }
    {   // ---- daily drop
        FakeStorage st;
        FakeClock clock;
        DailyDrop d(&st, &clock);
        check(d.claimable() && d.dayIndex() == 0, "first drop claimable");
        check(d.claim() == 0 && !d.claimable() && d.claim() == -1, "once a day");
        clock.nextDay();
        check(d.claimable() && d.dayIndex() == 1, "next day: step 2");
        for (int i = 0; i < 6; ++i) { d.claim(); clock.nextDay(); }
        check(d.dayIndex() == 0, "the ladder repeats after 7");
    }
    {   // ---- level
        FakeStorage st;
        Tuning t;
        Progression p(&st, t);
        check(p.level() == 1 && p.add(t.levelBaseXp - 1) == 0, "not yet");
        check(p.add(1) == 1 && p.level() == 2 && p.xp() == 0, "level up");
        check(p.add(10000) > 1, "multiple levels at once");
    }
    {   // ---- age screen: neutral, blocks play until answered, decides the ad audience
        FakeStorage st;
        FakeClock clock;
        FakeAds ads;
        GameServices s;
        s.storage = &st; s.clock = &clock; s.ads = &ads;
        Game g(s);
        step(g, 0.5f);
        check(g.screen() == Game::Screen::AgeGate, "first launch: the age screen");
        g.press();
        check(g.state() == Game::State::Title, "can't play before answering");
        g.tapButton("Continue");
        check(g.screen() == Game::Screen::AgeGate, "no year picked: Continue does nothing");
        g.tapButton("year-1"); // current year - 1 -> a child
        step(g, 0.1f);
        g.tapButton("Continue");
        check(g.profile().audience() == Audience::Child && ads.child, "a recent year: child audience, child-directed ads");
        FakeStorage st2;
        FakeAds ads2;
        s.storage = &st2; s.ads = &ads2;
        Game g2(s);
        step(g2, 0.5f);
        g2.tapButton("year-10"); g2.tapButton("year-10"); g2.tapButton("year-1"); // 21 years ago
        step(g2, 0.1f);
        g2.tapButton("Continue");
        check(g2.profile().audience() == Audience::Teen && !ads2.child, "21 years ago: 13+ audience");
        Game g3(s);
        check(g3.screen() != Game::Screen::AgeGate, "asked only once");
    }
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
    {   // ---- challenge links
        FakeStorage st = FakeStorage::ready();
        FakeClock clock;
        GameServices s;
        s.storage = &st; s.clock = &clock;
        Game g(s);
        g.openLink("https://cuckoostack-staging.web.app/c?d=2026-10-07&m=300&n=Sam");
        check(g.challengeMeters() == 0, "yesterday's challenge link: expired, no marker");
        g.openLink("https://cuckoostack-staging.web.app/c?d=2026-10-08&m=412&n=Sam%20K");
        check(g.challengeMeters() == 412, "today's challenge link: marker at 412 m");
        Game g2(s);
        check(g2.challengeMeters() == 412, "the challenge survives a relaunch the same day");
        clock.nextDay();
        Game g3(s);
        check(g3.challengeMeters() == 0, "and is gone the next day");
        FakeStorage kid; kid.kv["cluckstack-profile"] = "aud=1;";
        s.storage = &kid;
        Game g4(s);
        g4.openLink("cuckoostack://c?d=2026-10-09&m=50");
        check(g4.challengeMeters() == 0, "children: challenge links are ignored");
    }
    return finish("daily");
}
