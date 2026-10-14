// Streak, missions, daily drop, level and the age screen (Daily.h), with a clock moved by hand.
// (Reminder planning: Reminders_test.cpp.)
#include "TestUtil.h"
#include "core/Daily.h"

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
    {   // ---- challenge links
        FakeStorage st = FakeStorage::ready();
        FakeClock clock;
        GameServices s;
        s.storage = &st; s.clock = &clock;
        Game g(s);
        // challenges are a distance on a course level (l=); old daily-course links (d=, no level) are expired
        g.openLink("https://cuckoostack-staging.web.app/c?d=2026-10-07&m=300&n=Sam");
        check(g.challengeMeters() == 0, "an old daily-course link (no level): expired, no marker");
        g.openLink("https://cuckoostack-staging.web.app/c?l=1&m=412&n=Sam%20K");
        check(g.challengeMeters() == 412, "a level-1 challenge link: marker at 412 m");
        Game g2(s);
        check(g2.challengeMeters() == 412, "the challenge survives a relaunch");
        clock.nextDay();
        Game g3(s);
        check(g3.challengeMeters() == 412, "and the next day (it lasts until that level is cleared)");
        st.kv["cluckstack-campaign"] = "level=2;";
        Game g5(s);
        g5.press(); // a run on level 2 drops the level-1 challenge
        check(g5.challengeMeters() == 0, "once level 1 is behind you, its challenge is gone");
        g5.openLink("https://cuckoostack-staging.web.app/c?l=1&m=90");
        check(g5.challengeMeters() == 0, "a link to an already-cleared level is ignored");
        FakeStorage kid; kid.kv["cluckstack-profile"] = "aud=1;";
        s.storage = &kid;
        Game g4(s);
        g4.openLink("cuckoostack://c?l=1&m=50");
        check(g4.challengeMeters() == 0, "children: challenge links are ignored");
    }
    {   // ---- progression: XP to the next level, several levels at once, persistence
        FakeStorage st;
        Tuning t;
        Progression p(&st, t);
        check(p.level() == 1 && p.xpToNext() == t.levelBaseXp && p.fraction() == 0.f, "level 1: base XP to the next");
        check(p.add(0) == 0 && p.add(-5) == 0 && p.xp() == 0, "no XP for nothing (or negative)");
        check(p.add(t.levelBaseXp / 2) == 0 && std::abs(p.fraction() - 0.5f) < 0.01f, "half way to level 2");
        const int need = (t.levelBaseXp - t.levelBaseXp / 2) + (t.levelBaseXp + t.levelStepXp) + 10;
        check(p.add(need) == 2 && p.level() == 3 && p.xp() == 10, "enough XP for two levels: both, with the rest carried");
        check(p.xpToNext() == t.levelBaseXp + 2 * t.levelStepXp, "each level needs a step more XP");
        Progression again(&st, t);
        check(again.level() == 3 && again.xp() == 10, "level and XP survive a relaunch");
        again.debugSetLevel(20);
        check(again.level() == 20 && again.xp() == 0, "developer: jump to a level");
    }
    {   // ---- missions: all done, the bonus, every description
        FakeStorage st;
        FakeClock clock;
        Missions m(&st, &clock);
        check(!m.allDone() && !m.bonusClaimed(), "a new day's missions: none done");
        for (const Mission& x : m.list()) {
            if (x.kind == MissionKind::Distance) m.recordDistance(x.target); else m.record(x.kind, x.target);
        }
        check(m.allDone(), "all three done");
        m.claimBonus();
        Missions again(&st, &clock);
        check(again.bonusClaimed() && again.allDone(), "the bonus claim survives a relaunch");
        clock.nextDay();
        again.refresh();
        check(!again.bonusClaimed() && !again.allDone(), "a new day: new missions, bonus available again");
        bool described = true;
        for (int k = 0; k < int(MissionKind::Count); ++k)
            for (int target : {1, 3}) {
                const std::string d = Missions::describe({MissionKind(k), target});
                described = described && !d.empty() && d.find("surge") == std::string::npos;
            }
        check(described, "every mission kind has a description (none say \"surge\")");
    }
    {   // ---- profile: sessions, install day, the usual hour, flags and counters
        FakeStorage st;
        FakeClock clock; // 10:00 on 2026-10-08
        {
            Profile p(&st, &clock);
            check(p.sessions() == 0 && p.daysSinceInstall() == 0 && p.usualHour() == 19, "a new profile: no sessions, usual hour 19 by default");
            p.sessionStarted();
            clock.t += 9 * 3600; p.sessionStarted(); // 19:00
            clock.nextDay(); p.sessionStarted();      // 19:00 the next day
            check(p.sessions() == 3 && p.installDay() == civilDay("2026-10-08") && p.daysSinceInstall() == 1, "sessions counted from the install day");
            check(p.usualHour() == 19, "the usual hour: when most sessions start");
            p.setFlag("replays", true);
            p.setCounter("starter_seen", 2);
        }
        Profile again(&st, &clock);
        check(again.sessions() == 3 && again.flag("replays") && again.counter("starter_seen") == 2 && !again.flag("other"),
              "sessions, flags and counters survive a relaunch");
    }
    {   // ---- streak: played today, the developer shortcut
        FakeStorage st;
        FakeClock clock;
        Streak s(&st, &clock);
        check(!s.playedToday(), "not played yet today");
        s.played();
        check(s.playedToday() && s.count() == 1, "played: counts today");
        s.debugSet(5, 2);
        check(s.status() == Streak::Status::Saveable, "developer: a 5-day streak that missed yesterday is saveable");
    }
    return finish("Daily");
}
