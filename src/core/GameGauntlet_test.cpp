// Maze gauntlets in play (GameGauntlet.cpp), in the real Game at 60 Hz. Level 11's course carries the tomb at about
// 40%: brought to its door's height, a forecourt (its door being low, ending under a lintel as low as the door), the
// tomb, a porch at its exit's height. She's in once she reaches its door (it shuts behind her), runs either way inside
// at full speed, turns back off bumpers and gates set against her, and comes out onto the porch heading right. No
// OVERDRIVE inside and no droid raids; a crash inside is revived at its door. The camera eases out inside, leading her
// way. Its back walls are inscribed with hieroglyphs, lit up round her as she passes. The bot clears level 11, tomb and
// all, on several seeds; near misses a beat apart chain (the slow motion only on a chain's first); a blade crossed both
// ways takes an egg each way. (Those on the levels and gauntlets they were written round: tests/fixtures.) And on the
// game's own levels: every gauntlet in one is come to at its door's height, and the bot gets out of it showing the
// near misses the solver counts.
#include "TestUtil.h"
#include "core/Campaign.h"
#include "core/Game.h"
#include "core/Hieroglyphs.h"
#include "core/Level.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

using namespace cs;
using namespace test;

namespace {

struct Env {
    FakeStorage st = FakeStorage::ready();
    FakeClock clock;
    FakeAds ads;
    explicit Env(int level = 11) { ads.state = RewardedState::Unavailable; Campaign(&st).debugSetLevel(level); }
    GameServices svc() { GameServices s; s.storage = &st; s.clock = &clock; s.ads = &ads; return s; }
};

void placement() {
    Level l;
    const int level = 11;
    l.reset(Campaign::seedOf(level));
    l.setDifficulty(35.0 * (level - 1), 0.35);
    l.setFinish(Campaign::lengthOf(level));
    l.setSaws(Campaign::sawsOf(level), Campaign::sawRowOf(level));
    l.setGauntlet(Campaign::gauntletOf(level), Campaign::gauntletAt(level));
    l.generateUntil(l.finish() + 20);
    check(l.gauntlets.size() == 1 && l.gauntlets[0].g == gauntletNamed("tomb"), "level 11: its course carries the tomb");
    if (l.gauntlets.empty()) return;
    const PlacedGauntlet& p = l.gauntlets[0];
    check(std::abs(p.x0 - Campaign::gauntletAt(level)) < 25, "...at about 40% of the way");
    const Segment* forecourt = nullptr;
    const Segment* lintel = nullptr;
    const Segment* span = nullptr;
    const Segment* after = nullptr;
    for (size_t i = 0; i < l.segs.size(); ++i)
        if (l.segs[i].gauntlet) { span = &l.segs[i]; lintel = &l.segs[i - 1]; forecourt = &l.segs[i - 2]; after = &l.segs[i + 1]; }
    check(span && std::abs(span->x0 - p.x0) < 1e-9 && std::abs(span->x1 - p.x1()) < 1e-9 && l.heightAt((p.x0 + p.x1()) / 2) == 0,
          "...its span on the course (the walls in it are its grid's)");
    const Curve cv = l.curveAt(p.x0);
    check(forecourt && forecourt->h == p.g->entryRow() && !forecourt->ceil && lintel->x1 - forecourt->x0 >= 8 * U - 1e-9 &&
              lintel->x1 - forecourt->x0 >= cv.speed * 0.9 - 1e-9 && lintel->x1 - forecourt->x0 >= 2.5 * cv.speed * cv.regen * 1.1 - 1e-9,
          "...a forecourt at its door's height: a reaction's run long, and two and a half eggs' refill (its first saws count on them)");
    check(lintel && lintel->h == p.g->entryRow() && lintel->ceil == p.g->entryTop() && p.g->entryTop() == 5 &&
              std::abs(lintel->x1 - lintel->x0 - Level::kLintel * U) < 1e-9,
          "...its door low (4 high), the forecourt ends under its lintel: a barrier as low as the door");
    check(after && after->h == p.g->exitRow() && after->x1 - after->x0 >= 8 * U - 1e-9 && !after->ceil, "...a porch at its exit's height");
    int inside = 0, cells = 0;
    for (const Saw& w : l.saws) inside += l.gauntletAt(w.x) != nullptr;
    for (int c = 0; c < p.g->cols(); ++c)
        for (int r = 0; r < p.g->rows(); ++r) cells += p.g->sawAt(c, r) ? 1 : 0;
    check(cells > 20 && inside == cells, "...its saws join the course's (all of them)");
    const double x = p.x0 + 20.5 * U; // (a column through the tunnel where its roof dips, the middle floor, the hall)
    check(std::abs(l.groundBelow(x, 20 * U) - 14 * U) < 1e-9 && std::abs(l.groundBelow(x, 9 * U) - 6 * U) < 1e-9 &&
              std::abs(l.groundBelow(x, 4 * U) - 1 * U) < 1e-9,
          "...the floor under a point inside it is its grid's (the hall's, the middle floor's, the tunnel's)");
    Level other;
    other.reset(Campaign::seedOf(12));
    other.setGauntlet(Campaign::gauntletOf(12), Campaign::gauntletAt(12));
    other.setFinish(Campaign::lengthOf(12));
    other.generateUntil(other.finish());
    check(other.gauntlets.empty(), "level 12: none (a gauntlet every other level)");
}

float henX(const Game& g) { return g.hen().nodes[size_t(g.hen().root)].pos.x; }

// The bot, jumped to the door of `level`'s gauntlet, run till it's out (or not): the near misses shown inside
struct Through { bool out = false; int nearMisses = 0, perfects = 0, closeCalls = 0; };
Through through(int level, uint64_t seed = 1) {
    Env env(level);
    Game g(env.svc());
    g.debugSeed(seed);
    g.debugSetBot({true, false, false, 0});
    g.debugNearGauntlet();
    g.press();
    int p0 = -1, c0 = -1;
    Through t;
    for (int i = 0; i < 60 * 90 && g.state() == Game::State::Play && !t.out; ++i) {
        g.update(1.0 / 60.0);
        if (g.inGauntlet() && p0 < 0) p0 = g.perfects(), c0 = g.closeCalls();
        t.out = p0 >= 0 && !g.inGauntlet();
    }
    t.perfects = g.perfects() - std::max(0, p0);
    t.closeCalls = g.closeCalls() - std::max(0, c0);
    t.nearMisses = t.perfects + t.closeCalls;
    return t;
}

// Every level with a gauntlet (the game's own levels: their data says which, assets/levels): its course as authored
// brings her to the door at its height (a barrier just before it no lower than the door) and takes her on from a porch
// at its exit's; the bot, jumped to the door, gets out, and the game shows the near misses the solver counts on its way
// (15 or more each, within 2 of the solver's)
void everyLevel() {
    std::string wrong, failed, counts;
    int gauntlets = 0;
    for (int level = 1; level <= 30; ++level) {
        const Gauntlet* want = Campaign::gauntletOf(level);
        if (!want) continue;
        ++gauntlets;
        Level l;
        Campaign c(nullptr);
        c.debugSetLevel(level);
        c.applyTo(l);
        l.generateUntil(l.finish() + 20);
        bool placed = l.gauntlets.size() == 1 && l.gauntlets[0].g == want;
        for (size_t i = 1; placed && i < l.segs.size(); ++i)
            if (l.segs[i].gauntlet)
                placed = l.segs[i - 1].h == want->entryRow() && (!l.segs[i - 1].ceil || l.segs[i - 1].ceil == want->entryTop()) &&
                         (i + 1 >= l.segs.size() || l.segs[i + 1].h == want->exitRow());
        if (!placed) wrong += " " + std::to_string(level);
        const Through t = through(level);
        const int solved = want->solve().nearMisses;
        std::printf("      level %d, the %s: %s, %d near misses shown (%d PERFECT, %d CLOSE ...), the solver's %d\n", level, want->name().c_str(),
                    t.out ? "out" : "NOT OUT", t.nearMisses, t.perfects, t.closeCalls, solved);
        if (!t.out) failed += " " + std::to_string(level);
        else if (t.nearMisses < 15 || std::abs(t.nearMisses - solved) > 2) counts += " " + std::to_string(level);
    }
    if (!wrong.empty()) std::printf("  not as it should be (level):%s\n", wrong.c_str());
    check(gauntlets > 0 && wrong.empty(), "every level with a gauntlet: its course comes to the door at its height (a barrier before it no lower "
                                          "than the door) and goes on from a porch at its exit's");
    check(failed.empty(), "...the bot, jumped to each one's door, gets out");
    check(counts.empty(), "...and the game shows the near misses the solver counts on its way (15 or more each)");
}

// Near misses a beat apart chain (Game::closeCall): down the ascent's Descending Passage a CLOSE SHAVE lands on every
// step, a CLOSE CALL between, all one chain ("x2", "x3" ... past "x20"), and only its first brings on the slow motion
void chains() {
    Env env(19);
    Game g(env.svc());
    g.resize(430, 900);
    g.debugRecordHud(true);
    g.debugSeed(1);
    g.debugSetBot({true, false, false, 0});
    g.debugNearGauntlet();
    g.press();
    int longest = 0, dips = 0, shaves = 0;
    bool slow = false;
    double door = 0;
    for (int i = 0; i < 60 * 30 && g.state() == Game::State::Play; ++i) {
        g.update(1.0 / 60.0);
        if (g.inGauntlet() && door == 0) door = g.course().gauntlets.front().x0;
        if (door == 0) continue;
        if (henX(g) > door + 130 * U) break; // (the foot of the passage)
        for (const auto& h : g.hudStrings()) {
            const size_t x = h.text.find("\u00d7");
            if (h.text.rfind("CLOSE", 0) == 0 && x != std::string::npos) longest = std::max(longest, std::atoi(h.text.c_str() + x + 2));
        }
        const bool now = g.timeScale() < 0.6f;
        dips += now && !slow;
        slow = now;
        shaves = g.closeCalls();
    }
    std::printf("      down the Descending Passage: %d close calls and shaves, a chain to x%d, %d slow-motion dips\n", shaves, longest, dips);
    check(shaves >= 20 && longest >= 20, "near misses a beat apart chain: down the ascent's Descending Passage, a CLOSE SHAVE on every step and "
                                         "a CLOSE CALL between, one chain past x20");
    check(dips >= 1 && dips <= 2, "...and only the chain's first brings on the slow motion (the beat stays quick)");
}

// A blade crossed both ways takes an egg each way (GameSaw.cpp re-arms it once she's clear of it, as Gauntlet::step has
// it): the wells' turnstile, crossed going right and again coming back
void turnstile() {
    Env env(15);
    Game g(env.svc());
    g.debugSeed(1);
    g.debugSetBot({true, false, false, 0});
    g.debugNearGauntlet();
    g.press();
    double x = -1;
    int cuts = 0;
    bool was = false;
    for (int i = 0; i < 60 * 90 && g.state() == Game::State::Play; ++i) {
        g.update(1.0 / 60.0);
        if (x < 0 && g.inGauntlet()) x = g.course().gauntlets.front().x0 + 217.5 * U; // (the middle tier's end, under the hole)
        for (const Saw& s : g.saws())
            if (std::abs(s.x - x) < 1e-6 && s.y == 5) {
                const bool cut = s.state == Saw::State::Cut;
                cuts += cut && !was;
                was = cut;
            }
        if (x > 0 && !g.inGauntlet()) break;
    }
    check(g.state() == Game::State::Play && cuts == 2, "a blade crossed both ways takes an egg each way: the wells' turnstile cuts twice");
}

void inscriptions() {
    // the tomb's back walls, inscribed like the Pyramid Texts (Level::inscribe)
    Level l;
    const int level = 11;
    l.reset(Campaign::seedOf(level));
    l.setDifficulty(35.0 * (level - 1), 0.35);
    l.setFinish(Campaign::lengthOf(level));
    l.setGauntlet(Campaign::gauntletOf(level), Campaign::gauntletAt(level));
    l.generateUntil(l.finish() + 20);
    l.rebuildBlocks();
    if (l.gauntlets.empty()) { check(false, "the tomb, to inscribe"); return; }
    const PlacedGauntlet& p = l.gauntlets[0];
    const Gauntlet& g = *p.g;
    using Cell = Gauntlet::Cell;
    auto cellOf = [&](const glm::vec3& v) { return std::pair<int, int>{int(std::floor((v.x - p.x0) / U)), int(std::floor(v.y / U))}; };
    auto runDown = [&](int c, int r) { int n = 0; while (r - n >= 0 && g.at(c, r - n) == Cell::Air) ++n; return n; };
    std::map<std::pair<int, int>, const Level::GSign*> at;
    bool onWall = true, dado = true, doors = true, starsTop = true;
    int signs = 0, stars = 0, birds = 0;
    for (const Level::GSign& s : l.gsigns) {
        const auto [c, r] = cellOf(s.pos);
        at[{c, r}] = &s;
        onWall = onWall && g.at(c, r) == Cell::Air && std::abs(s.pos.z - (-DEPTH / 2 + 0.01f)) < 1e-4f;
        dado = dado && g.at(c, r - 1) == Cell::Air;
        doors = doors && c > 0 && c < g.cols() - 1;
        if (s.kind == 1) { ++stars; starsTop = starsTop && s.cp == hieroglyphs::kStar && g.at(c, r + 1) != Cell::Air && runDown(c, r) >= 7; }
        if (s.kind == 0) {
            ++signs;
            birds += std::find(hieroglyphs::kColumns.begin(), hieroglyphs::kColumns.begin() + hieroglyphs::kBirds, s.cp) !=
                     hieroglyphs::kColumns.begin() + hieroglyphs::kBirds;
        }
    }
    bool varied = true;
    for (const auto& [cr, s] : at) {
        const auto up = at.find({cr.first, cr.second + 1});
        varied = varied && !(s->kind == 0 && up != at.end() && up->second->kind == 0 && up->second->cp == s->cp);
    }
    check(signs > 1000 && stars > 100, "the tomb's back walls are inscribed: columns of hieroglyphs, stars along its tall rooms' tops");
    check(onWall && doors, "...every sign just in front of the back wall of an open cell (none on stone, a gate or a doorway)");
    check(dado, "...none in the cell on a floor (the dado, behind her feet)");
    check(starsTop, "...the stars only along the top of rooms 7 or more high");
    check(varied && birds * 4 > signs, "...no sign twice running down a column; birds and the egg come up often (a hen's tomb)");
    // the great hall's cartouches: "Cuckoo", read top down
    bool named = !l.gcartouches.empty();
    for (const Level::GCartouche& k : l.gcartouches) {
        std::vector<const Level::GSign*> in;
        for (const Level::GSign& s : l.gsigns)
            if (s.kind == 2 && std::abs(s.pos.x - k.pos.x) < 1e-4f) in.push_back(&s);
        std::sort(in.begin(), in.end(), [](const Level::GSign* a, const Level::GSign* b) { return a->pos.y > b->pos.y; });
        named = named && in.size() == 4 && k.pos.y > 14 * Uf;
        for (size_t i = 0; named && i < 4; ++i) named = in[i]->cp == hieroglyphs::kCuckoo[i] && std::abs(in[i]->pos.y - k.pos.y) < k.size.y / 2;
    }
    check(named, "...and the great hall's cartouches name it: \"Cuckoo\" (k-w-k-w), read top down");
}

void inscriptionsLit() {
    // in play: the signs are drawn (glyphs just in front of the back wall), lit up round her, dim further off
    Env env;
    Game g(env.svc());
    g.resize(430, 900);
    g.debugSetBot({true, false, false, 0});
    g.debugNearGauntlet();
    g.press();
    for (int i = 0; i < 60 * 3 && g.state() == Game::State::Play && !g.inGauntlet(); ++i) g.update(1.0 / 60.0);
    step(g, 0.3f);
    const glm::vec2 lamp{henX(g), g.henBase() + float(g.eggCount()) * Uf + 0.55f};
    float nearSum = 0, farSum = 0;
    int nearN = 0, farN = 0;
    for (const Instance& i : g.renderList().worldText) {
        if (std::abs(i.model[3].z - (-DEPTH / 2 + 0.01f)) > 1e-3f) continue;
        const float d = glm::length(glm::vec2(i.model[3].x, i.model[3].y) - lamp), b = std::max({i.color.r, i.color.g, i.color.b});
        if (d < 1.2f) { nearSum += b; ++nearN; }
        if (d > 5.f) { farSum += b; ++farN; }
    }
    check(g.inGauntlet() && nearN > 3 && farN > 20, "in the tomb its hieroglyphs are drawn, round her and further off");
    check(nearN && farN && nearSum / nearN > 4.f * farSum / farN, "...lit up round her (several times brighter than further off)");
}

void inside() {
    {   // straight to the tomb, nobody stacking: in through the door, which shuts; the tunnel's first saw gets her
        Env env;
        Game g(env.svc());
        g.debugNearGauntlet();
        g.press();
        const double door = g.course().gauntlets.empty() ? 0 : g.course().gauntlets[0].x0;
        bool entered = false;
        float speed = 0;
        for (int i = 0; i < 60 * 6 && g.state() == Game::State::Play; ++i) {
            const float before = henX(g);
            g.update(1.0 / 60.0);
            if (g.inGauntlet()) { entered = true; speed = std::max(speed, (henX(g) - before) * 60.f); }
        }
        const double col = (henX(g) - door) / U;
        check(entered && speed > 0.95f * float(g.course().curveAt(door).speed), "Jump to gauntlet: in through the tomb's door, running at the run's full speed");
        check(g.state() == Game::State::Dead && col > 10 && col < 13, "...and with no egg under her, the tunnel's first saw gets her");
    }
    // the bot playing: through the tunnel and its gate into the shaft, where she bounces (the gate set against her)
    // while she stacks; stacked 5 high she climbs out over its wall and runs left along the middle floor
    Env env;
    Game g(env.svc());
    g.debugSetBot({true, false, false, 0});
    g.debugNearGauntlet();
    g.press();
    const double door = g.course().gauntlets.empty() ? 0 : g.course().gauntlets[0].x0;
    bool shaft = false, bounced = false, up = false, along = false;
    for (int i = 0; i < 60 * 15 && g.state() == Game::State::Play && !along; ++i) {
        g.update(1.0 / 60.0);
        const double col = (henX(g) - door) / U;
        shaft = shaft || (g.inGauntlet() && col > 43 && col < 47);
        bounced = bounced || (shaft && !up && g.runDirection() < 0);
        up = up || g.henBase() > 5.5f * Uf;
        along = up && col < 30 && g.runDirection() < 0;
    }
    check(shaft && bounced, "...the bot playing: through the tunnel and its gate into the shaft, bouncing there while she stacks");
    check(up && along, "...stacked 5 high she climbs out over its wall, and runs left along the middle floor");
}

void camera() {
    // inside, the camera eases out, further back than on the run, and leads her way: on a phone held upright (the
    // narrowest view), what she'll reach in half a second is on screen
    auto metre = [](const Game& g) { // a metre across, on screen at her depth (HUD points)
        glm::vec2 a{0.f}, b{0.f};
        const glm::vec3 p = g.hen().nodes[size_t(g.hen().root)].pos;
        g.worldOnScreen(p, a);
        g.worldOnScreen(p + glm::vec3(1.f, 0.f, 0.f), b);
        return b.x - a.x;
    };
    float run = 0;
    {   // on the run (level 11's start, the intro over)
        Env env;
        Game g(env.svc());
        g.resize(430, 900);
        g.press();
        step(g, 2.f);
        run = metre(g);
    }
    Env env;
    Game g(env.svc());
    g.resize(430, 900);
    g.debugSetBot({true, false, false, 0});
    g.debugNearGauntlet();
    g.press();
    for (int i = 0; i < 60 * 3 && g.state() == Game::State::Play && !g.inGauntlet(); ++i) g.update(1.0 / 60.0);
    step(g, 0.45f); // (the camera eased out; she's in the tunnel, running right, before its first saw)
    const float in = metre(g);
    const float speed = float(g.course().curveAt(henX(g)).speed);
    glm::vec2 px{0.f};
    const bool seen = g.worldOnScreen({henX(g) + speed * 0.5f * float(g.runDirection()), g.henBase(), 0.f}, px) && px.x > 0 && px.x < 430;
    check(g.inGauntlet() && run > 0 && in < run * 0.9f, "inside a gauntlet the camera eases out, further back than on the run");
    check(seen, "...and leads her way: on a phone held upright, what she'll reach in half a second is on screen");
}

void botClears() {
    std::string failed;
    for (int seed = 1; seed <= 4; ++seed) {
        Env env;
        Game g(env.svc());
        g.debugSeed(uint64_t(seed));
        g.debugSetBot({true, false, false, 0});
        g.press();
        bool inside = false, left = false;
        float lowest = 1e9f;
        for (int i = 0; i < 60 * 150 && g.state() == Game::State::Play; ++i) {
            g.update(1.0 / 60.0);
            if (g.inGauntlet()) { inside = true; left = left || g.runDirection() < 0; lowest = std::min(lowest, g.henBase()); }
        }
        if (!g.lastRunCleared() || !inside || !left || lowest > 1.5f * Uf) failed += " " + std::to_string(seed);
    }
    if (!failed.empty()) std::printf("  not cleared (seed):%s\n", failed.c_str());
    check(failed.empty(), "the bot clears level 11, tomb and all (in, both ways, down to its bottom hall, out), on every seed");
}

void noRaidsNoOverdrive() {
    {   // OVERDRIVE ends at the door
        Env env;
        Game g(env.svc());
        g.debugNearGauntlet();
        g.press();
        g.update(1.0 / 60.0);
        g.debugStartSurge();
        for (int i = 0; i < 60 * 3 && g.state() == Game::State::Play && !g.inGauntlet(); ++i) g.update(1.0 / 60.0);
        check(g.inGauntlet() && !g.surgeStatus().surging && g.surgeStatus().graceT == 0, "OVERDRIVE ends at the door: no smashing through a maze");
    }
    {   // no droid raids inside
        Env env;
        Game g(env.svc());
        g.debugSetBot({true, false, false, 0});
        g.debugNearGauntlet();
        g.press();
        bool raided = false, was = false;
        for (int i = 0; i < 60 * 25 && g.state() == Game::State::Play; ++i) {
            const int fired = g.droidShotsFired();
            g.update(1.0 / 60.0);
            if (g.inGauntlet() && ((g.droidRaiding() && !was) || g.droidShotsFired() > fired)) raided = true;
            was = g.droidRaiding();
        }
        check(!raided, "...and no droid raids inside (none starts, none fires)");
    }
}

// The level editor's play-test (Game::debugPlayTest): every run on its level, from where it says. Along the course:
// on the wall there, the meter full and no eggs, the run counted from there; a crash or a clear starts the next run
// there again, on that level (not the next). In its maze: inside at once, at that cell, running the way it says, and
// the bot gets out from there
void playTest() {
    auto nextRun = [](Game& g) { // (through the results to the next run)
        for (int i = 0; i < 60 * 20 && g.state() != Game::State::Play; ++i) { g.update(1.0 / 60.0); if (i % 20 == 0) g.press(); }
    };
    {
        Env env(13);
        Game g(env.svc());
        g.debugPlayTest({13, 120.0});
        g.press();
        const double floor = g.course().heightAt(120) * U;
        check(std::abs(g.furthest() - 120) < 1e-6 && std::abs(double(g.henBase()) - floor) < 1e-6 && g.eggCount() == 0 && g.meterEggs() >= 8 - 1e-3,
              "the level editor's play-test from 120 m along level 13: she starts there, on the wall there, the meter full, no eggs");
        dieQuickly(g);
        nextRun(g);
        check(g.state() == Game::State::Play && g.courseLevel() == 13 && std::abs(g.furthest() - 120) < 1e-6, "...after a crash, the next run starts there again");
    }
    {
        Env env(13);
        Game g(env.svc());
        g.debugSeed(1);
        g.debugSetBot({true, false, false, 0});
        const double near = Campaign::lengthOf(13) - 30;
        g.debugPlayTest({13, near});
        g.press();
        for (int i = 0; i < 60 * 30 && g.state() == Game::State::Play; ++i) g.update(1.0 / 60.0);
        const bool cleared = g.lastRunCleared();
        nextRun(g);
        check(cleared && g.courseLevel() == 13 && std::abs(g.furthest() - near) < 1e-6, "...and after a clear, the same level again from there (not the next)");
    }
    const Gauntlet* tomb = Campaign::gauntletOf(11);
    if (!tomb) { check(false, "level 11's tomb, to play-test in"); return; }
    Gauntlet::State at{};
    bool found = false;
    for (const Gauntlet::TraceStep& t : tomb->botTrace()) // (a cell on the bot's way, on an upper floor, nothing stacked)
        if (!found && t.s.n == 0 && t.s.b >= 5 && t.move == Gauntlet::Move::Ok) { at = t.s; found = true; }
    if (!found) { check(false, "a cell on the tomb's way with nothing stacked, to start from"); return; }
    Env env(11);
    Game g(env.svc());
    g.debugSeed(1);
    g.debugSetBot({true, false, false, 0});
    g.debugPlayTest({11, -1, at.c, at.b, at.d});
    g.press();
    const PlacedGauntlet& p = g.course().gauntlets.front();
    check(g.inGauntlet() && g.runDirection() == at.d && std::abs(double(g.henBase()) - at.b * U) < 1e-6 && std::abs(g.furthest() - (p.x0 + (at.c + 0.5) * U)) < 1e-6,
          "...from a cell of level 11's tomb: in it at once, at that cell, running its way");
    bool out = false;
    for (int i = 0; i < 60 * 90 && g.state() == Game::State::Play && !out; ++i) { g.update(1.0 / 60.0); out = !g.inGauntlet(); }
    check(out && g.state() == Game::State::Play, "...and the bot gets out from there");
}

void revive() {
    Env env;
    env.ads.state = RewardedState::Ready;
    Game g(env.svc());
    g.debugNearGauntlet();
    g.press();
    const double door = g.course().gauntlets.empty() ? 0 : g.course().gauntlets[0].x0;
    for (int i = 0; i < 60 * 3 && !g.inGauntlet(); ++i) g.update(1.0 / 60.0);
    // stack into the tunnel's roof: a crash
    for (int i = 0; i < 60 * 10 && g.state() == Game::State::Play; ++i) { if (i % 8 == 0) g.press(); g.update(1.0 / 60.0); }
    check(g.state() == Game::State::Dead && henX(g) < door + 8 * U, "a crash inside (stacked into the tunnel's roof)");
    step(g, Game::kCrashTail + 0.2f);
    g.tapButton("Watch ad");
    for (int i = 0; i < 60 && g.state() != Game::State::Play; ++i) g.update(1.0 / 60.0);
    check(g.state() == Game::State::Play && g.inGauntlet() && henX(g) < door + 1.5 * U && g.eggCount() == 0,
          "...revived back in at its door, with no eggs");
}

} // namespace

int main() {
    everyLevel(); // (the game's own levels: every gauntlet in one played through)
    test::useFixtures(); // (from here: the levels and gauntlets these tests are written round, tests/fixtures)
    placement();
    inscriptions();
    inscriptionsLit();
    inside();
    camera();
    botClears();
    chains();
    turnstile();
    noRaidsNoOverdrive();
    revive();
    playTest();
    return finish("GameGauntlet");
}
