// Headless acceptance checks for Surge mode (Surge.h), driving the real Game at a fixed 60 Hz step.
#include "core/Game.h"

#include <cstdio>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}
void run(cs::Game& g, float seconds) { for (int i = 0; i < int(seconds * 60); ++i) g.update(1.0 / 60.0); }
} // namespace

int main() {
    using namespace cs;
    {   // 3. surge: invulnerable, smashes walls, x1.35 speed, points per block
        Game g({});
        g.press(); // start the run; never lay an egg
        run(g, 0.3f);
        const float baseSpeed = g.surgeStatus().speed;
        g.debugStartSurge();
        check(g.surgeStatus().surging && g.surgeStatus().chain == 0, "surge starts, chain reset to 0");
        run(g, 1.0f);
        const float ratio = g.surgeStatus().speed / baseSpeed;
        std::printf("      speed ratio %.2f\n", ratio);
        check(ratio > 1.25f && ratio < 1.45f, "speed is ~x1.35 during the surge");
        const int scoreBefore = g.score();
        run(g, 3.8f);
        check(g.state() == Game::State::Play, "hen never dies during the 5 s surge (no eggs laid)");
        const auto st = g.surgeStatus();
        std::printf("      smashed %d blocks, score %d -> %d\n", st.smashed, scoreBefore, g.score());
        check(st.smashed > 0, "walls in her path were destroyed");
        check(st.partyK > 0.95f, "party mode fully on");
        check(g.liveSmashDebris() <= surge::DEBRIS_POOL, "debris stays inside the fixed pool");
        run(g, 0.3f);
        check(!g.surgeStatus().surging && g.surgeStatus().graceT > 0, "surge ended after 5 s, grace running");
        check(g.state() == Game::State::Play, "alive during grace");
        // 4. after surge + grace, normal rules: with no eggs she must die at the next wall
        run(g, 20.f);
        check(g.state() == Game::State::Dead, "normal collision and death rules apply after the grace period");
        check(g.surgeStatus().partyK < 0.05f, "party mode eased back out");
    }
    {   // 2. a missed ball resets the chain outside a surge
        Game g({});
        g.press();
        g.debugSetChain(5, 60.f); // grace keeps her alive so balls can pass behind her; not surging
        run(g, 25.f);
        check(g.surgeStatus().chain == 0, "missing a ball outside a surge resets the chain");
    }
    {   // 5. restart mid-surge resets music, visuals, HUD and timers
        Game g({});
        g.press();
        run(g, 0.3f);
        g.debugStartSurge();
        run(g, 1.f);
        g.debugSetChain(0, 0.f);
        // force a death by ending invulnerability: wait for the surge to finish and the wall to kill her
        run(g, 30.f);
        if (g.state() == Game::State::Dead) { run(g, 1.f); g.press(); }
        run(g, 0.05f);
        const auto st = g.surgeStatus();
        check(g.state() == Game::State::Play && !st.surging && st.chain == 0 && st.graceT == 0 && st.smashed == 0 &&
                  g.liveSmashDebris() == 0, "restart resets surge state, timers and debris");
        check(st.partyK < 0.01f, "restart resets party visuals");
    }
    std::printf("%s\n", failures ? "SURGE TESTS FAILED" : "all surge checks passed");
    return failures ? 1 : 0;
}
