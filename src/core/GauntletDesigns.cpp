#include "GauntletDesigns.h"

#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <tuple>

namespace cs {

namespace {

// a state with no way out from it, whatever she stacks: a dead end
bool deadEnd(const Gauntlet& g, Gauntlet::State s0) {
    std::set<std::tuple<int, int, int, int, int>> seen;
    std::deque<Gauntlet::State> q{s0};
    while (!q.empty()) {
        const Gauntlet::State s = q.front();
        q.pop_front();
        if (!seen.insert({s.c, s.b, s.n, s.d, s.k}).second) continue;
        for (int lay = 0; lay <= 1; ++lay) {
            Gauntlet::State o;
            const Gauntlet::Move m = g.step(s, lay, o);
            if (m == Gauntlet::Move::Exit) return false;
            if (m != Gauntlet::Move::Crash) q.push_back(o);
        }
    }
    return true;
}

std::string n(int v) { return std::to_string(v); }

} // namespace

int designForks(const Gauntlet& g, int* bounced) {
    int forks = 0;
    *bounced = 0;
    for (const Gauntlet::TraceStep& t : g.botTrace()) {
        const int a = t.s.c + t.s.d;
        int k = 0;
        while (g.at(a, t.s.b + k) == Gauntlet::Cell::Bumper) ++k;
        if (k < 2 || (t.move != Gauntlet::Move::Bounce && t.info.climbed < k)) continue;
        Gauntlet::State other = t.s, o;
        other.k = 1;                                          // (no egg laid there: she has what she has)
        other.n = t.move == Gauntlet::Move::Bounce ? k : 0;   // the other way: stacked to its top, or with none
        const Gauntlet::Move m = g.step(other, 0, o);
        if (m == Gauntlet::Move::Crash || (m == Gauntlet::Move::Bounce) == (t.move == Gauntlet::Move::Bounce)) continue;
        if (!deadEnd(g, o)) continue;
        ++forks;
        *bounced += t.move == Gauntlet::Move::Bounce;
    }
    return forks;
}

std::vector<DesignCheck> designChecks(const Gauntlet& g) {
    std::vector<DesignCheck> out;
    const std::vector<Gauntlet::TraceStep> way = g.botTrace();
    if (g.name() == "gallery") {
        // the Grand Gallery: 15 steps climbed one egg at a time, each exact, the roof right over the egg she carries
        int run = 0, best = 0;
        bool brushing = false; // (her head at the roof as she came to the step)
        for (const Gauntlet::TraceStep& t : way) {
            if (t.info.climbed) {
                run = t.info.climbed == 1 && t.info.exact && brushing ? run + 1 : 0;
                best = std::max(best, run);
            }
            brushing = t.info.squeeze && t.s.n + t.lay == 1;
        }
        out.push_back({"The Grand Gallery: 15 steps climbed one egg at a time, every one exact, no room for two", best >= 15,
                       n(best) + " in a row on the bot's way"});
    } else if (g.name() == "wells") {
        // the wells: she lands on a blade at the foot of every well, the stack fallen with her; the turnstile crosses
        // one blade both ways
        int landings = 0;
        std::map<std::pair<int, int>, int> sawnWays; // (a blade's cell: the ways it took an egg, as bits)
        for (const Gauntlet::TraceStep& t : way) {
            if (t.info.fell && t.info.sawn) ++landings;
            if (t.info.sawn) sawnWays[{t.s.c + t.s.d, t.s.b + t.info.climbed - t.info.fell}] |= t.s.d > 0 ? 1 : 2;
        }
        bool both = false;
        for (const auto& [cell, ways] : sawnWays) both = both || ways == 3;
        out.push_back({"The Sacred Wells: a blade at the foot of every well (five), landed on with the stack", landings >= 5, n(landings) + " landed on"});
        out.push_back({"...and the turnstile: one blade crossed both ways, an egg each", both, both ? "crossed both ways" : "no blade crossed both ways"});
    } else if (g.name() == "labyrinth") {
        int bounced = 0;
        const int forks = designForks(g, &bounced);
        out.push_back({"The Labyrinth: three forks or more (stack over the bumper or bounce back: one way a dead end), one where back is the way",
                       forks >= 3 && bounced >= 1, n(forks) + " forks, " + n(bounced) + " where back is the way"});
    } else if (g.name() == "ascent") {
        // the Descending Passage: twelve steps down, each landed on a blade that takes her last egg, in a row; the
        // King's Chamber: out over its wall with an egg to spare, the cube saw past it taking it; down the air shaft
        int run = 0, best = 0;
        bool chamber = false, shaft = false;
        for (size_t i = 0; i < way.size(); ++i) {
            const Gauntlet::TraceStep& t = way[i];
            if (t.info.sawn) { run = t.info.lastEgg && t.info.fell == 1 ? run + 1 : 0; best = std::max(best, run); }
            if (t.info.climbed >= 5 && !t.info.exact && t.s.n + t.lay == t.info.climbed + 1 && i + 1 < way.size() && way[i + 1].info.lastEgg &&
                g.at(way[i + 1].s.c + way[i + 1].s.d, way[i + 1].s.b - 1) == Gauntlet::Cell::CubeSaw)
                chamber = true;
            shaft = shaft || (t.info.fell >= 15 && t.info.sawn);
        }
        out.push_back({"The Pharaoh's Ascent: the Descending Passage, twelve steps down onto a blade each, every one her last egg", best >= 12,
                       n(best) + " in a row on the bot's way"});
        out.push_back({"...the King's Chamber: out over its wall with one egg to spare (the cube saw past it takes that one)", chamber,
                       chamber ? "on the bot's way" : "not on the bot's way"});
        out.push_back({"...and down the air shaft, fifteen rows or more, onto a blade", shaft, shaft ? "on the bot's way" : "not on the bot's way"});
    }
    return out;
}

} // namespace cs
