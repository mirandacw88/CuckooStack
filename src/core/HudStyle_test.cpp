// The HUD look (HudStyle.h): the web build's CSS palette and easing curves, the rise-in animation every overlay uses,
// and number grouping. (The OVERDRIVE line, the Level Complete banner and fitWidth: tests/polish_test.cpp.)
#include "TestUtil.h"
#include "core/HudStyle.h"

using namespace cs;
using namespace cs::hud;
using namespace test;

int main() {
    check(css("#29e7ff") == srgbColor("#29e7ff") && kNeon == css("#29e7ff") && kHot == css("#ff2bd6"), "css: colours as authored (sRGB)");
    check(easeOut(0) == 0 && easeOut(1) == 1 && easeOut(-1) == 0 && easeOut(2) == 1, "easeOut: 0 -> 1, clamped");
    check(easeOut(0.5f) > 0.8f, "easeOut: a strong ease-out (most of the way by halfway)");
    check(std::abs(easeOutBack(0)) < 1e-6f && std::abs(easeOutBack(1) - 1) < 1e-6f, "easeOutBack: 0 -> 1");
    float peak = 0;
    for (float t = 0; t <= 1; t += 0.01f) peak = std::max(peak, easeOutBack(t));
    check(peak > 1.05f, "easeOutBack: overshoots before settling");
    check(seg(5, 4, 6) == 0.5f && seg(3, 4, 6) == 0 && seg(9, 4, 6) == 1, "seg: progress through a range, clamped");
    const Up start = up(0.f, 0.f), half = up(0.3f, 0.2f), done = up(2.f, 0.f), early = up(0.1f, 0.3f);
    check(start.alpha == 0 && start.dy == 18.f, "up: starts invisible, 18 px low");
    check(done.alpha == 1 && done.dy == 0, "up: ends in place, opaque");
    check(early.alpha == 0 && half.alpha > 0 && half.alpha < 1, "up: waits for its delay");
    check(grouped(0) == "0" && grouped(999) == "999" && grouped(1000) == "1,000" && grouped(1250) == "1,250", "grouped: thousands");
    check(grouped(1234567) == "1,234,567" && grouped(-1250) == "-1,250" && grouped(-999) == "-999", "grouped: millions, negatives");
    return finish("HudStyle");
}
