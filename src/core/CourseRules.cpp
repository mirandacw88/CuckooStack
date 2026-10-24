#include "CourseRules.h"

#include <algorithm>
#include <cmath>

namespace cs::rules {

double fallSeconds(int drop) { return std::sqrt(2.0 * drop * U / kGravity); }

double marginScale(int hard) { return 1.0 - 0.1 * std::clamp(hard - 5, 0, 5); }

double landingRun(double speed, int drop, double m) { return speed * (fallSeconds(drop) + 0.45 * m); }

double sawAfterDrop(double speed, double react, int drop) { return std::max(react, speed * fallSeconds(drop) + 0.3); }

bool takesBlockOver(const Segment& s, const Segment* next, double feetY) {
    if (!s.top) return false;
    if (feetY >= s.top * U - 0.05 || s.ceil < s.h + 2 || s.ceil * U < feetY + 2 * U - 0.05) return true;
    return next && !next->top && next->h >= s.top;
}

double springLaunch(int power) { return std::sqrt(2.0 * kGravity * power * U); }

Flight springFlight(const Level& l, const Spring& s, double speed, int eggs) {
    Flight f;
    const double hb = 0.22, dt = 1.0 / 240, end = std::min(l.generatedTo(), l.finish() > 0 ? l.finish() + 20 : 1e9);
    double x = s.x, base = s.floor * U, vy = springLaunch(s.power);
    f.apexX = x; f.apexY = base;
    int steps = 0;
    auto mark = [&] { f.path.push_back(x); f.path.push_back(base); };
    mark();
    while (x < end && steps < 240 * 6) {
        x += speed * dt; ++steps;
        const double top = base + eggs * U;
        // the wall ahead (Game::updatePlay's): its face below her stack's top crashes her; with eggs over it she steps on
        const double hFront = l.floorAt(x + hb, std::max(base, top)) * U;
        if (hFront > base + 0.05) {
            const int k = static_cast<int>(std::ceil((hFront - base) / U - 0.5 - 1e-6));
            if (top < hFront - 0.06 || k > eggs) { f.end = Flight::End::Wall; f.x = x; f.y = base; mark(); return f; }
            base = hFront;
            f.end = Flight::End::Landed; f.x = x; f.y = base; f.floor = static_cast<int>(std::lround(base / U)); mark();
            return f;
        }
        vy -= kGravity * dt;
        base += vy * dt;
        if (base > f.apexY) { f.apexY = base; f.apexX = x; }
        // her head under a barrier (a floating block she's over, or stacked to climb onto: not a barrier)
        const double henTop = base + eggs * U + HEN_H;
        for (const Segment& sg : l.segs) {
            if (sg.x0 > x + 0.25) break;
            if (!sg.ceil || x + 0.25 < sg.x0 || x + 0.22 >= sg.x1) continue;
            if (sg.top && std::max(base, base + eggs * U) >= sg.top * U - 0.05) continue;
            if (henTop > sg.ceil * U + 0.02) { f.end = Flight::End::Barrier; f.x = x; f.y = base; mark(); return f; }
        }
        const double support = std::max(l.floorAt(x - hb, base), l.floorAt(x + hb, base)) * U;
        if (vy < 0 && base <= support) {
            base = support;
            f.end = Flight::End::Landed; f.x = x; f.y = base; f.floor = static_cast<int>(std::lround(base / U)); mark();
            return f;
        }
        if (steps % 12 == 0) mark();
    }
    f.end = Flight::End::Off; f.x = x; f.y = base;
    return f;
}

int forecourtCells(double speed, double epu) {
    return std::max({8, static_cast<int>(std::ceil(speed * 0.9 / U)), static_cast<int>(std::ceil(2.5 / epu))});
}

bool doorNeedsLintel(const Gauntlet& g) { return g.entryTop() - g.entryRow() - Gauntlet::kBodyRows < static_cast<int>(Level::kBudgetCap); }

} // namespace cs::rules
