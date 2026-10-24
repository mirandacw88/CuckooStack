// The developer menu's bot (staging builds): it plays the run for you, reading the course the game built and stacking
// just enough eggs for the next wall, and one more for each saw on the way (a saw slices one off); under barriers: as
// high as the lowest allows, minus two; up to a spring, nothing for what it flies her over, and nothing in the air. Options: carry two
// spare eggs (so the droid has something to raid), play sloppy (now and then it lays one egg too few), and keep
// going (it declines the continue and starts the next run by itself).
#include "Game.h"

#include "CourseRules.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace cs {

void Game::updateBot(float rdt) {
    if (!bot_.on) return;
    bot_.cooldown -= rdt;
    if (state_ == State::Dead) { // keep going: decline the continue, then start the next run
        if (!bot_.loop) return;
        bot_.restartT += rdt;
        if (bot_.restartT < 1.6f) return;
        bot_.restartT = 0;
        if (screen_ == Screen::Continue) { tapButton("No thanks"); return; }
        press(); // a game-over screen's main action, or the next run
        return;
    }
    bot_.restartT = 0;
    if (state_ != State::Play || screen_ != Screen::None || bot_.cooldown > 0) return;
    if (gIn_.g) { // in a maze gauntlet: the solver's policy (Gauntlet::shouldLay), for the step her front edge makes next
        if (vy_ != 0 || gFly_) return; // (not while dropping down a hole, or flying off a spring)
        // at the solver's tap rate: an egg every kLayGap cells at the most (not again in the cell it laid in)
        const int since = gMoves_ - gBotLay_;
        if (since < 1) return;
        // (just bounced: still in the column she bounced in, as the solver has her, though her front edge, now behind
        // her, is a column back: the solver lays there, and the bot would never ask)
        const double front = x_ + (gBounced_ ? -gDir_ : gDir_) * 0.22;
        const Gauntlet::State s{std::clamp(gIn_.col(front), 0, gIn_.g->cols() - 1), static_cast<int>(std::lround(baseY_ / U)),
                                static_cast<int>(eggs_.size()), gDir_, std::max(0, Gauntlet::kLayGap - since), zapped_ ? 1 : 0};
        // (nor once her front edge is over a spring cell: she's launched as her middle reaches its middle, and the solver has no
        // state there, its step flies her at once)
        if (gIn_.g->springAt(s.c, s.b - 1)) return;
        // (and only with room over where she is too: her back half may still be under a lower ceiling)
        const int c = gIn_.col(x_), feet = s.b + s.n + 1;
        bool room = c < 0 || c >= gIn_.g->cols();
        if (!room) { room = true; for (int r = feet; r < feet + Gauntlet::kBodyRows; ++r) room = room && !gIn_.g->solid(c, r); }
        if (room && s.n < Gauntlet::kMaxEggs && gIn_.g->shouldLay(s)) {
            const size_t before = eggs_.size();
            lay();
            bot_.cooldown = 0.08f;
            if (eggs_.size() > before) gBotLay_ = gMoves_;
        }
        return;
    }
    if (launched_) return; // (off a spring: the arc does the rest)
    const double x = x_, top = baseY_ + double(eggs_.size()) * U;
    double want = 0;
    // what's coming within 6.5 m, in order: walls to stand on (their tops), saws to cross (an egg over the surface),
    // lava (an egg a cell over it) and lasers (from where she comes into its reach); each saw passed on the way takes an
    // egg off what's stacked now, lava an egg a cell, a laser she can't pass short one
    struct Need { double at, top; int eats; int beam = -1, floor = 0; };
    std::vector<Need> ahead;
    // a floating block: over it where there's no room under it (from the street or from what she stands on, a lower
    // block in a stair of them), where it bridges onto a wall as high as its top, or once she's up on it; else under
    // it (its underside her barrier)
    auto over = [&](const Segment& s) {
        const Segment* next = nullptr;
        for (const Segment& n : level_.segs)
            if (n.x0 >= s.x1 - 1e-6) { next = &n; break; }
        return rules::takesBlockOver(s, next, baseY_);
    };
    for (const Segment& s : level_.segs) {
        if (s.x1 <= x) continue;
        if (s.x0 > x + 6.5) break;
        const int floor = over(s) ? s.top : s.h;
        if (s.x0 <= x) want = std::max(want, (floor + bot_.spare) * U); // (spare eggs over the floor here)
        else if (s.x1 > x + 0.22) ahead.push_back({s.x0, floor * U, 0});
    }
    for (const Saw& w : level_.saws) {
        if (w.state != Saw::State::Armed || cutting(w) || w.x + Level::kSawReach < x) continue;
        if (w.x - Level::kSawReach > x + 6.5) break;
        ahead.push_back({w.x - Level::kSawReach, (w.y + 1) * U, 1});
    }
    for (const Lava& f : level_.lavas) { // (on it: the cells still to come)
        if (f.x1 <= x || f.x0 > x + 6.5) continue;
        const int left = x < f.x0 ? f.cells() : f.cells() - 1 - static_cast<int>(std::floor((x - f.x0) / U));
        if (left > 0) ahead.push_back({std::max(f.x0, x + 0.01), (f.floor + left) * U, left});
    }
    for (Laser z : level_.lasers) {
        if (!level_.traceLaser(z) || z.x1 <= z.x0 || z.x0 > x + 6.5 || z.x1 <= x + 0.22) continue;
        if (zapped_ && x + 0.22 >= z.x0) continue; // (it's had its egg this pass)
        if (x + 0.22 >= z.x0 && z.row < level_.heightAt(x)) continue; // (in it, over its row: it's under her)
        const int from = level_.heightAt(std::max(x, z.x0 - 0.3)); // (the floor she comes into it from)
        if (level_.laserTakesAnEgg(z)) ahead.push_back({std::max(z.x0, x + 0.01), (std::max(from, z.row) + 1) * U, 1, z.row, from});
        else ahead.push_back({std::max(z.x0, x + 0.01), 0, 0, z.row, from});
    }
    for (const Spring& sp : level_.springs) { // a spring she'll run onto: nothing past it stacked for (she flies it)
        if (sp.fired || sp.x < x - 0.05 || std::abs(sp.floor * U - baseY_) > 0.06) continue;
        if (sp.x > x + 6.5) break;
        ahead.push_back({sp.x - 0.3, 0, 0, -2});
        break;
    }
    std::sort(ahead.begin(), ahead.end(), [](const Need& a, const Need& b) { return a.at < b.at; });
    int eaten = cutSawX_ >= 0 ? 1 : 0; // (the egg being sawn now is as good as gone)
    double cap = 1e9;
    for (const Need& n : ahead) {
        if (n.beam == -2) break; // (a spring: what's past it is flown over)
        if (n.beam >= 0 && n.eats) { // a laser she can't pass short: an egg in its row (from above: one under her) to cut out
            want = std::max(want, n.top + eaten * U);
            ++eaten;
            continue;
        }
        if (n.beam >= 0) { // ...one she can: under it short (her feet two rows under its row), nothing past it stacked for yet
            cap = std::max(0, n.beam - Gauntlet::kBodyRows + eaten) * U;
            break;
        }
        want = std::max(want, n.top + eaten * U);
        eaten += n.eats;
    }
    want = std::min(want, cap);
    // under (or about to pass under) barriers: as high as the lowest of them allows, minus two
    double safe = 1e9;
    for (const Segment& s : level_.segs)
        if (s.ceil && !over(s) && x + 0.25 >= s.x0 - 3 && x + 0.22 < s.x1) safe = std::min(safe, std::floor((s.ceil * U - HEN_H - 0.02) / U + 1e-6) * U);
    if (safe < 1e8) want = std::min(std::max(want, safe - U * 2), safe);
    if (bot_.sloppy && want > 0) { // now and then, one egg short for a wall (one roll per wall)
        const double next = level_.heightAt(x + 3.0) * U;
        if (next > baseY_ + 0.05 && std::abs(next - bot_.rolledFor) > 1e-6) { bot_.rolledFor = next; bot_.short_ = rng_.next01() < 0.17f; }
        if (bot_.short_) want -= U;
    }
    if (top + 0.05 < want) { lay(); bot_.cooldown = 0.08f; }
}

} // namespace cs
