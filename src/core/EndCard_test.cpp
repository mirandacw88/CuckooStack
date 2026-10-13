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

} // namespace

int main() {
    endCard();
    return finish("EndCard");
}
