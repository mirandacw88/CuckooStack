#include "LevelBake.h"

#include "Campaign.h"
#include "LevelBeats.h"
#include "LevelJudge.h"

#include <algorithm>
#include <cmath>

namespace cs {

bool GenOptions::has(const char* id) const { return only.empty() || std::find(only.begin(), only.end(), id) != only.end(); }

LevelData bakeLevel(int n, const Gauntlet* g, std::optional<uint32_t> seed, const GenOptions& options, std::vector<ChunkUse>* used, std::vector<std::string>* dropped) {
    if (used) used->clear();
    if (dropped) dropped->clear();
    if (g) return bakeClassic(n, g, seed, options);
    beats::Options o;
    o.hazards = options;
    o.density = options.density;
    beats::Plan p = beats::compose(n, seed ? *seed : Campaign::seedOf(n), o);
    if (used) *used = p.chunks;
    if (dropped) *dropped = p.chunksDropped;
    return std::move(p.data);
}

LevelData bakeClassic(int n, const Gauntlet* g, std::optional<uint32_t> seed, const GenOptions& options) {
    const double length = 300.0 + 10.0 * (n - 1);
    Level l;
    l.reset(seed ? *seed : Campaign::seedOf(n));
    l.setDifficulty(35.0 * (n - 1), 0.35);
    l.setFinish(length);
    l.setSaws(options.has("saw") ? Campaign::sawsOf(n) : 0, Campaign::sawRowOf(n));
    l.setLasers(options.has("laser") ? Campaign::lasersOf(n) : 0);
    l.setLava(options.has("lava") ? Campaign::lavaOf(n) : 0);
    l.setCorn(options.has("disco"));
    l.setGauntlet(g, 0.4 * length);
    l.generateUntil(length + 20);
    LevelData d;
    d.number = n;
    d.length = length;
    d.diffOffset = 35.0 * (n - 1);
    d.diffScale = 0.35;
    d.notes = {"Level " + std::to_string(n) + ", " + zoneName(n) + (g ? ": the " + g->name() + " at 40% of the way" : std::string()) + "."};
    double reach = 0;
    for (const Segment& s : l.segs) {
        if (s.x0 >= length - 1e-6) break;
        LevelData::Wall w;
        w.cells = int(std::lround((s.x1 - s.x0) / U));
        if (s.gauntlet) {
            for (const PlacedGauntlet& p : l.gauntlets) if (std::abs(p.x0 - s.x0) < 1e-6) w.gauntlet = p.g->name();
        } else {
            w.h = s.h; w.kind = s.kind; w.ceil = s.ceil;
        }
        d.course.push_back(w);
        reach = s.x1;
    }
    for (const Saw& w : l.saws) if (!l.gauntletAt(w.x) && w.x < reach) d.items.push_back({&kSawHazard, {w.x}});
    for (const Corn& c : l.corns) if (c.x < reach) d.items.push_back({&kDiscoHazard, {c.x, c.y}});
    for (const Laser& z : l.lasers) if (!l.gauntletAt(z.x - U * 0.5) && !l.gauntletAt(z.x + U * 0.5) && z.x < reach) d.items.push_back({&kLaserHazard, {z.x, double(z.row), z.dir > 0 ? 1.0 : 0.0}});
    for (const Lava& f : l.lavas) if (!l.gauntletAt(f.x0) && f.x1 <= reach) d.items.push_back({&kLavaHazard, {f.x0, double(f.cells())}});
    for (const Spring& s : l.springs) if (!l.gauntletAt(s.x) && s.x < reach) d.items.push_back({&kSpringHazard, {s.x, double(s.power)}});
    return d;
}

} // namespace cs
