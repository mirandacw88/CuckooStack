// Difficulty (Difficulty.cpp): the web build's S-curve over the first 350 m, and the invisible adaptive difficulty
// (Dda) that eases a struggling player's runs and tightens them after a breakthrough, saved between launches.
#include "TestUtil.h"
#include "core/Difficulty.h"

using namespace cs;
using namespace test;

namespace {

void sCurve() {
    const Curve a = curve(0), m = curve(175), b = curve(350), far = curve(5000), neg = curve(-40);
    check(a.D == 0 && b.D == 1 && std::abs(m.D - 0.5) < 1e-12, "D: 0 at the start, 0.5 halfway, 1 at 350 m");
    check(far.D == 1 && far.speed == b.speed && far.cap == b.cap, "past 350 m: a flat plateau");
    check(neg.D == 0 && neg.speed == a.speed, "before the start: clamped to the start");
    check(a.speed == 8.0 && b.speed == 12.4 && a.regen == 0.5 && b.regen == 0.4, "speed 8 -> 12.4 m/s, refill 0.5 -> 0.4 s per egg");
    // eggs per metre: never much stingier at full speed than at the start (a slow refill at top speed starved the
    // later levels: 0.92 s per egg at 12.4 m/s is one egg every 11.4 m)
    check(b.speed * b.regen < 1.3 * a.speed * a.regen, "...so an egg refills every ~5 m at full speed (4 m at the start)");
    check(a.cap == 4 && b.cap == 8 && a.maxUp == 1 && b.maxUp == 4, "walls: up to 4 eggs tall, then 8; steps up 1 -> 4");
    check(a.minLen == 7 && a.maxLen == 10 && b.minLen == 3 && b.maxLen == 5, "segments get shorter (7-10 -> 3-5)");
    check(a.gap == 4 && b.gap == 3, "barrier clearance 4 eggs -> 3");
    bool monotone = true;
    for (double d = 0; d < 360; d += 5) {
        const Curve c0 = curve(d), c1 = curve(d + 5);
        monotone = monotone && c1.D >= c0.D && c1.speed >= c0.speed && c1.cap >= c0.cap && c1.drop <= c0.drop && c1.ceil >= c0.ceil;
    }
    check(monotone, "only ever harder with distance (speed, walls, barriers up; drops down)");
    check(curve(30).D - curve(0).D < curve(190).D - curve(160).D, "eases in: the first metres barely change");
}

void adaptive() {
    FakeStorage st;
    Dda dda(&st);
    const float start = dda.assist();
    check(start > 0.f && dda.level(0) == start, "a new player starts with a little assist");
    check(dda.speedMul(0) < 1.f && dda.regenMul(0) < 1.f, "assist slows the run and speeds the egg refill");
    dda.record(100, 0);          // first run sets the skill
    dda.record(40, 100);         // much worse than usual
    const float struggling = dda.assist();
    check(struggling > start, "much worse runs: more assist");
    dda.record(200, 100);        // a breakthrough past the best
    check(dda.assist() < struggling, "a breakthrough past the best: a little less assist");
    for (int i = 0; i < 40; ++i) dda.record(1, 0);
    check(dda.assist() <= 0.55f && dda.level(0) <= 0.8f, "assist is capped (0.55 adaptive, 0.8 with boosts)");
    for (int i = 0; i < 60; ++i) dda.record(float(2000 + i * 200), float(1800 + i * 200));
    check(dda.assist() >= -0.35f && dda.assist() < 0.f, "a strong player: negative assist (faster), floored at -0.35");
    check(dda.speedMul(0) > 1.f && dda.regenMul(0) > 1.f, "negative assist speeds the run up");
    check(dda.speedMul(0) <= 1.f + 0.24f * 0.35f + 1e-6f, "...gently (at most +8.4% speed)");
}

void stuckZone() {
    FakeStorage st;
    Dda dda(&st);
    dda.newDay("2026-10-13");
    for (int i = 0; i < 3; ++i) dda.record(150.f + float(i), 0);
    check(dda.zoneStrength() > 0.f && std::abs(dda.zoneCenter() - 151.f) < 2.f, "three deaths near the same spot: a help zone there");
    const float atZone = dda.level(dda.zoneCenter()), farAway = dda.level(dda.zoneCenter() + 300.f);
    check(atZone > farAway, "the help is strongest at that spot, gone far past it");
    check(dda.level(dda.zoneCenter() - 40.f) > dda.level(dda.zoneCenter() + 40.f), "it ramps in before the spot more gently than it fades after");
    dda.record(260, 0); // well past the zone
    check(dda.zoneStrength() < 0.34f, "getting well past it fades the zone");
    dda.newDay("2026-10-14");
    check(dda.zoneStrength() == 0.f, "a new day: the zone and deaths reset");
}

void persistence() {
    FakeStorage st;
    {
        Dda dda(&st);
        dda.newDay("2026-10-13");
        dda.record(120, 0);
        dda.record(60, 120);
    }
    Dda again(&st);
    Dda fresh(nullptr);
    check(again.assist() != fresh.assist() || again.level(0) != fresh.level(0), "assist survives a relaunch");
    FakeStorage st2;
    {
        Dda a(&st2);
        a.newDay("2026-10-13");
        for (int i = 0; i < 3; ++i) a.record(150.f, 0);
    }
    Dda b(&st2);
    check(b.zoneStrength() > 0.f && std::abs(b.zoneCenter() - 150.f) < 1.f, "the help zone survives a relaunch");
    b.setBoost(0.2f);
    check(b.level(500) > Dda(&st2).level(500), "setBoost adds assist on top (not saved)");
}

void corruptSave() {
    bool survived = true;
    for (const char* raw : {"garbage", "0 0.1 3 2026-10-13 0 0 12,abc,7", "0 0.1 3 2026-10-13 0 0 99999999999999999999", "", "1 2"}) {
        FakeStorage st;
        st.kv["cluckstack-dda"] = raw;
        try {
            Dda d(&st);
            const float l = d.level(100);
            survived = survived && l >= -0.35f && l <= 0.8f;
            d.record(50, 0);
        } catch (...) {
            survived = false;
        }
    }
    check(survived, "a corrupted save never crashes the game (it starts from sane values)");
}

} // namespace

int main() {
    sCurve();
    adaptive();
    stuckZone();
    persistence();
    corruptSave();
    return finish("Difficulty");
}
