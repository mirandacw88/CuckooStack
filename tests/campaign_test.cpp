// Course levels (Campaign.h): seeded per level, a finish line, Level Cleared, retry on a crash.
#include "TestUtil.h"
#include "../src/core/Campaign.h"

using namespace cs;
using test::check;

namespace {
struct Env {
    test::FakeStorage st = test::FakeStorage::ready();
    test::FakeClock clock;
    test::FakeAds ads;
    GameServices svc() {
        ads.state = RewardedState::Unavailable; // no Continue offers in these checks
        GameServices s;
        s.storage = &st; s.ads = &ads; s.clock = &clock;
        return s;
    }
};
std::vector<Segment> courseOf(int level, double until) {
    Campaign c(nullptr);
    c.debugSetLevel(level);
    Level course;
    c.applyTo(course);
    course.generateUntil(until);
    return course.segs;
}
bool same(const std::vector<Segment>& a, const std::vector<Segment>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].x0 != b[i].x0 || a[i].x1 != b[i].x1 || a[i].h != b[i].h || a[i].ceil != b[i].ceil) return false;
    return true;
}
} // namespace

int main() {
    {   // ---- the same course for everyone; every level different
        check(same(courseOf(3, 400), courseOf(3, 400)), "level 3 is the same course every time (and for every player)");
        check(!same(courseOf(1, 400), courseOf(2, 400)), "level 1 and level 2 are different courses");
        check(Campaign::lengthOf(1) == 300 && Campaign::lengthOf(10) == 390, "levels are 300 m, +10 m per level");
    }
    {   // ---- difficulty steps up per level
        Campaign c1(nullptr), c10(nullptr);
        c10.debugSetLevel(10);
        Level l1, l10;
        c1.applyTo(l1); c10.applyTo(l10);
        const Curve a = l1.curveAt(150), b = l10.curveAt(150);
        check(b.speed > a.speed + 2.0, "level 10 runs much faster than level 1");
        check(b.cap > a.cap && b.maxUp > a.maxUp, "level 10 has taller walls and bigger steps");
        check(l1.curveAt(0).speed <= curve(0).speed + 1e-9, "level 1 starts at the easiest point of the curve");
    }
    {   // ---- past the finish line: a flat runway
        Campaign c(nullptr);
        Level course;
        c.applyTo(course);
        course.generateUntil(c.length() + 80);
        bool flat = true;
        for (const Segment& s : course.segs)
            if (s.x0 >= c.length() && (s.h != 0 || s.ceil != 0)) flat = false;
        check(flat, "after the finish the course is flat, with no walls or barriers");
        check(course.corns.empty() || course.corns.back().x < c.length(), "no disco balls past the finish");
    }
    {   // ---- a crash retries the same level
        Env env;
        Game g(env.svc());
        check(g.courseLevel() == 1, "a new player starts on level 1");
        test::dieQuickly(g);
        check(g.state() == Game::State::Dead && !g.lastRunCleared(), "a crash is not a clear");
        check(g.courseLevel() == 1 && g.levelAttempts() == 1, "after a crash: still level 1, 1 attempt");
        test::step(g, 2.f);
        test::dieQuickly(g);
        check(g.courseLevel() == 1 && g.levelAttempts() == 2, "the retry is level 1 again (2 attempts)");
    }
    {   // ---- reaching the finish clears the level
        Env env;
        Game g(env.svc());
        const int coinsBefore = g.coins();
        g.debugNearFinish();
        g.press();                 // starts 30 m before the finish line
        g.debugStartSurge();       // invulnerable through the last walls
        for (int i = 0; i < 60 * 12 && g.state() == Game::State::Play; ++i) g.update(1.0 / 60.0);
        check(g.state() == Game::State::Dead && g.lastRunCleared(), "crossing the finish line ends the run as a clear");
        check(g.courseLevel() == 2 && g.levelAttempts() == 0, "the next level is level 2, with no attempts yet");
        check(g.coins() >= coinsBefore + 60, "clearing level 1 pays the clear bonus (50 + 10 per level)");
        Game again(env.svc());
        check(again.courseLevel() == 2, "the level is saved: a relaunch resumes on level 2");
    }
    return test::finish("campaign");
}
