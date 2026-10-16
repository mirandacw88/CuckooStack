// The developer menu's bot (staging builds): it plays the run for you, reading the course the game built and stacking
// just enough eggs for the next wall (under a barrier: as high as the barrier allows, minus two). Options: carry two
// spare eggs (so the droid has something to raid), play sloppy (now and then it lays one egg too few), and keep
// going (it declines the continue and starts the next run by itself).
#include "Game.h"

#include <cmath>

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
    const double x = x_, top = baseY_ + double(eggs_.size()) * U;
    double want = 0;
    for (const Segment& s : level_.segs) {
        if (s.x1 <= x) continue;
        if (s.x0 > x + 6.5) break;
        if (s.x0 <= x) want = std::max(want, (s.h + bot_.spare) * U); // (spare eggs over the floor here)
        else if (s.x1 > x + 0.22) want = std::max(want, s.h * U);
    }
    for (const Segment& s : level_.segs)
        if (s.ceil && x + 0.25 >= s.x0 - 3 && x + 0.22 < s.x1) {
            const double safe = std::floor((s.ceil * U - HEN_H - 0.02) / U + 1e-6) * U;
            want = std::min(std::max(want, safe - U * 2), safe);
        }
    if (bot_.sloppy && want > 0) { // now and then, one egg short for a wall (one roll per wall)
        const double next = level_.heightAt(x + 3.0) * U;
        if (next > baseY_ + 0.05 && std::abs(next - bot_.rolledFor) > 1e-6) { bot_.rolledFor = next; bot_.short_ = rng_.next01() < 0.17f; }
        if (bot_.short_) want -= U;
    }
    if (top + 0.05 < want) { lay(); bot_.cooldown = 0.08f; }
}

} // namespace cs
