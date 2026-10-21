// The Share Replay end card renderer (EndCard.cpp, Android's card): size, opaque, the headline and label drawn.
#include "TestUtil.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cs;
using namespace test;

namespace {

struct Env {
    FakeStorage st = settled();
    // a player who has already answered the replay / notification offers, so no panel covers the results
    static FakeStorage settled() {
        FakeStorage s = FakeStorage::ready();
        s.kv["cluckstack-profile"] = "aud=2;f_replay_asked=1;f_notif_asked=1;";
        return s;
    }
    FakeClock clock;
    FakeAds ads;
    FakeReplay replay;
    GameServices svc() { GameServices s; s.storage = &st; s.clock = &clock; s.ads = &ads; s.replay = &replay; return s; }
};

// ---------------------------------------------------------------- end card

void endCard() {
    Env env;
    Game g(env.svc());
    ReplayMeta m;
    m.distance = 215; m.label = "LEVEL 3";
    const std::vector<uint8_t> a = g.endCard(360, 640, m);
    check(a.size() == size_t(360) * 640 * 4, "end card: RGBA at the requested size");
    bool opaque = true, varied = false;
    for (size_t i = 0; i < a.size(); i += 4) {
        opaque = opaque && a[i + 3] == 255;
        varied = varied || a[i] != a[0] || a[i + 1] != a[1] || a[i + 2] != a[2];
    }
    check(opaque && varied, "end card: opaque, with something drawn on it");
    m.newBest = true;
    check(g.endCard(360, 640, m) != a, "end card: a new best has its own headline");
    m.newBest = false; m.label = "LEVEL 3 CLEARED";
    check(g.endCard(360, 640, m) != a, "end card: the label is drawn");
}

// one channel of a pixel of a w x h card, its x and y checked: a read off the card is a bug in the test, so it fails
// at once (an unchecked one reads the next row, or past the end of the image: a crash on some runs only)
uint8_t at(const std::vector<uint8_t>& px, int w, int h, int x, int y, int c) {
    if (x < 0 || y < 0 || x >= w || y >= h || px.size() != size_t(w) * h * 4) throw std::out_of_range("a pixel off the end card");
    return px[(size_t(y) * w + x) * 4 + c];
}

// the card at other sizes: proportional, never drawing outside its canvas
void sizes() {
    Env env;
    Game g(env.svc());
    ReplayMeta m;
    m.distance = 4321; m.label = "LEVEL 12 CLEARED"; m.newBest = true;
    bool ok = true;
    for (auto [w, h] : {std::pair{720, 1280}, {1080, 1920}, {120, 213}, {300, 2000}, {2000, 300}, {1, 1}}) {
        const std::vector<uint8_t> px = g.endCard(w, h, m);
        ok = ok && px.size() == size_t(w) * h * 4;
    }
    check(ok, "any size, even tiny or odd shapes, renders without overrunning the canvas");
    // half size looks like the full card scaled down: compare brightness on a coarse grid, of whole 16 px blocks (360
    // isn't a multiple of 16: the last part-block's strip is left out)
    const auto big = g.endCard(720, 1280, m), small = g.endCard(360, 640, m);
    double diff = 0;
    int n = 0;
    for (int y = 0; y + 16 <= 640; y += 16)
        for (int x = 0; x + 16 <= 360; x += 16) {
            double a = 0, b = 0;
            for (int dy = 0; dy < 16; ++dy)
                for (int dx = 0; dx < 16; ++dx) {
                    b += at(small, 360, 640, x + dx, y + dy, 1);
                    for (int k = 0; k < 4; ++k) a += at(big, 720, 1280, 2 * (x + dx) + k % 2, 2 * (y + dy) + k / 2, 1) / 4.0;
                }
            diff += std::abs(a - b) / 256.0;
            ++n;
        }
    check(diff / n < 12.0, "the layout scales with the width (a half-size card matches the full one)");
    // the brand block's coin: gold pixels under "Can you beat it?"
    const int W = 720, H = 1280, cy = int(H * 0.42f) + 260;
    int gold = 0;
    for (int y = cy - 40; y < cy + 40; ++y)
        for (int x = W / 2 - 40; x < W / 2 + 40; ++x) gold += at(big, W, H, x, y, 0) > 180 && at(big, W, H, x, y, 1) > 120 && at(big, W, H, x, y, 2) < 110;
    check(gold > 300, "the brand block: the spinning-coin logo");
    m.distance = 12;
    check(g.endCard(720, 1280, m) != big, "the distance is drawn");
}

} // namespace

int main() {
    test::useFixtures(); // (the levels and gauntlets these tests are written round: tests/fixtures)
    endCard();
    sizes();
    return finish("EndCard");
}
