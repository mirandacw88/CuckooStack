// The course's shared rules (CourseRules.h): the landing after a drop, a saw's first spot after one, the floating
// block's over-or-under, and a gauntlet's forecourt, as the generator, the bot and the checker all take them.
#include "TestUtil.h"
#include "core/CourseRules.h"
#include "core/Difficulty.h"

#include <cmath>

using namespace cs;
using namespace test;

namespace {

Segment seg(int h, int ceil = 0, int top = 0) {
    Segment s;
    s.x0 = 0; s.x1 = 10 * U; s.h = h; s.ceil = ceil; s.top = top;
    return s;
}

void landing() {
    check(std::abs(rules::fallSeconds(0)) < 1e-9, "no drop, no fall");
    check(rules::fallSeconds(9) > rules::fallSeconds(4) && rules::fallSeconds(4) > rules::fallSeconds(1), "a longer drop falls longer");
    const double speed = 12.4;
    check(std::abs(rules::landingRun(speed, 0) - speed * 0.45) < 1e-9, "even a step down asks for a reaction's run");
    check(rules::landingCells(speed, 10) * U >= rules::landingRun(speed, 10) && (rules::landingCells(speed, 10) - 1) * U < rules::landingRun(speed, 10),
          "the landing in cells: the fewest that cover the run");
    const double react = speed * 0.6;
    check(rules::sawAfterDrop(speed, react, 1) == react, "a saw after a small drop: a reaction is the longer wait");
    check(rules::sawAfterDrop(speed, react, 30) > react, "...after a deep one: the fall is");
}

void blocks() {
    const Segment street = seg(0), wall3 = seg(3);
    check(!rules::takesBlockOver(street, nullptr, 0), "a wall, not a floating block: never 'over'");
    check(!rules::takesBlockOver(seg(0, 3, 6), &street, 0), "a block with room under it, met from the street: under");
    check(rules::takesBlockOver(seg(0, 1, 3), &street, 0), "no room under it: over");
    check(rules::takesBlockOver(seg(0, 3, 6), &street, 6 * U), "already as high as its top: over");
    check(rules::takesBlockOver(seg(0, 3, 3 + 1), nullptr, 0) == false, "room under it and nothing past it: under");
    check(rules::takesBlockOver(seg(0, 2, 3), &wall3, 0), "bridging onto a wall as high as its top: over");
    check(rules::takesBlockOver(seg(0, 5, 8), &street, 4 * U), "met from a lower block, its underside too close over her: over (a stair of blocks)");
}

void forecourt() {
    const Curve c = curve(400);
    const int cells = rules::forecourtCells(c.speed, 0.1);
    check(cells >= 8 && cells * U >= c.speed * 0.9 - 1e-9, "a forecourt: at least a reaction's run, and 8 cells");
    check(rules::forecourtCells(c.speed, 0.01) > cells, "...longer where eggs refill slower");
}

} // namespace

int main() {
    landing();
    blocks();
    forecourt();
    return test::finish("CourseRules");
}
