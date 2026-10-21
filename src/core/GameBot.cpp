// The developer menu's bot (staging builds): it plays the run for you, reading the course the game built and stacking
// just enough eggs for the next wall, and one more for each saw on the way (a saw slices one off); under barriers: as
// high as the lowest allows, minus two. Options: carry two
// spare eggs (so the droid has something to raid), play sloppy (now and then it lays one egg too few), and keep
// going (it declines the continue and starts the next run by itself).
#include "Game.h"

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
        if (vy_ != 0) return; // (not while dropping down a hole)
        // at the solver's tap rate: an egg every kLayGap cells at the most (not again in the cell it laid in)
        const int since = gMoves_ - gBotLay_;
        if (since < 1) return;
        const Gauntlet::State s{std::clamp(gIn_.col(x_ + gDir_ * 0.22), 0, gIn_.g->cols() - 1), static_cast<int>(std::lround(baseY_ / U)),
                                static_cast<int>(eggs_.size()), gDir_, std::max(0, Gauntlet::kLayGap - since)};
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
    const double x = x_, top = baseY_ + double(eggs_.size()) * U;
    double want = 0;
    // what's coming within 6.5 m, in order: walls to stand on (their tops) and saws to cross (an egg over the surface);
    // each saw passed on the way takes an egg off what's stacked now
    struct Need { double at, top; bool saw; };
    std::vector<Need> ahead;
    for (const Segment& s : level_.segs) {
        if (s.x1 <= x) continue;
        if (s.x0 > x + 6.5) break;
        if (s.x0 <= x) want = std::max(want, (s.h + bot_.spare) * U); // (spare eggs over the floor here)
        else if (s.x1 > x + 0.22) ahead.push_back({s.x0, s.h * U, false});
    }
    for (const Saw& w : level_.saws) {
        if (w.state != Saw::State::Armed || cutting(w) || w.x + Level::kSawReach < x) continue;
        if (w.x - Level::kSawReach > x + 6.5) break;
        ahead.push_back({w.x - Level::kSawReach, (w.y + 1) * U, true});
    }
    std::sort(ahead.begin(), ahead.end(), [](const Need& a, const Need& b) { return a.at < b.at; });
    int sawsBefore = cutSawX_ >= 0 ? 1 : 0; // (the egg being sawn now is as good as gone)
    for (const Need& n : ahead) {
        want = std::max(want, n.top + sawsBefore * U);
        sawsBefore += n.saw;
    }
    // under (or about to pass under) barriers: as high as the lowest of them allows, minus two
    double safe = 1e9;
    for (const Segment& s : level_.segs)
        if (s.ceil && x + 0.25 >= s.x0 - 3 && x + 0.22 < s.x1) safe = std::min(safe, std::floor((s.ceil * U - HEN_H - 0.02) / U + 1e-6) * U);
    if (safe < 1e8) want = std::min(std::max(want, safe - U * 2), safe);
    if (bot_.sloppy && want > 0) { // now and then, one egg short for a wall (one roll per wall)
        const double next = level_.heightAt(x + 3.0) * U;
        if (next > baseY_ + 0.05 && std::abs(next - bot_.rolledFor) > 1e-6) { bot_.rolledFor = next; bot_.short_ = rng_.next01() < 0.17f; }
        if (bot_.short_) want -= U;
    }
    if (top + 0.05 < want) { lay(); bot_.cooldown = 0.08f; }
}

} // namespace cs
