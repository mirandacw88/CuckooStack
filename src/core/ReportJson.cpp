#include "ReportJson.h"

#include "CourseRules.h"

#include "BotRun.h"
#include "Campaign.h"
#include "CourseRules.h"
#include "Chunk.h"
#include "Gauntlet.h"
#include "GauntletReport.h"
#include "Hazards.h"
#include "Level.h"
#include "LevelCatalog.h"
#include "LevelJudge.h"

#include <algorithm>
#include <cstdio>
#include <optional>

namespace cs {

namespace {

// judged, and a note for each section this build doesn't know (a newer game's hazard: kept as it is, not played)
Report judged(const Level& l, const LevelData& d) {
    Report r = judge(l);
    for (const std::string& line : d.unknown)
        if (line.find_first_of(" \t0123456789.-") == std::string::npos && !line.empty())
            r.notes.push_back({0, 0, "A section this game doesn't know: '" + line + "' (kept as it is; a newer game's hazard?)"});
    return r;
}

} // namespace

std::string levelReportJson(const LevelData& d, bool game, const LevelCatalog* cat, const std::atomic<bool>* cancel) {
    Level l;
    l.load(d, cat);
    l.generateUntil(d.length + 20);
    const Report rep = judged(l, d);
    const Curve c0 = l.curveAt(0), c1 = l.curveAt(d.length);
    int sawsOnRun = 0;
    for (const Saw& w : l.saws) sawsOnRun += !l.gauntletAt(w.x) && w.x < d.length;
    std::string o = "{\"ok\":true,\"level\":" + std::to_string(d.number) + ",\"zone\":\"" + zoneName(d.number) + "\"";
    o += ",\"length\":" + jsonNum(d.length) + ",\"courseLength\":" + jsonNum(d.courseLength(cat)) + ",\"raids\":" + std::to_string(d.raids);
    o += ",\"speed\":[" + jsonNum(c0.speed) + "," + jsonNum(c1.speed) + "],\"regen\":[" + jsonNum(c0.regen) + "," + jsonNum(c1.regen) + "]";
    o += ",\"walls\":" + std::to_string(rep.walls) + ",\"barriers\":" + std::to_string(rep.barriers) + ",\"blocks\":" + std::to_string(rep.blocks) + ",\"tallest\":" + std::to_string(rep.tallest) +
         ",\"saws\":" + std::to_string(sawsOnRun) + ",\"discos\":" + std::to_string(l.corns.size()) + ",\"lowest\":" + jsonNum(rep.lowest);
    o += ",\"gauntlets\":[";
    for (size_t i = 0; i < l.gauntlets.size(); ++i) {
        const PlacedGauntlet& p = l.gauntlets[i];
        o += std::string(i ? "," : "") + "{\"name\":\"" + jsonEsc(p.g->name()) + "\",\"x0\":" + jsonNum(p.x0) + ",\"cols\":" + std::to_string(p.g->cols()) +
             ",\"rows\":" + std::to_string(p.g->rows()) + ",\"entryRow\":" + std::to_string(p.g->entryRow()) + ",\"entryTop\":" + std::to_string(p.g->entryTop()) +
             ",\"exitRow\":" + std::to_string(p.g->exitRow()) + "}";
    }
    o += "]";
    // each spring's flight at the curve's speed there (CourseRules.h's springFlight): the editors draw its arc
    o += ",\"arcs\":[";
    bool firstArc = true;
    for (const Spring& s : l.springs) {
        if (l.gauntletAt(s.x) || l.zoneAt(s.x)) continue; // (a zone's: flown by its grid's rules, the maze's arc)
        const rules::Flight f = rules::springFlight(l, s, l.curveAt(s.x).speed);
        static const char* ends[] = {"landed", "wall", "barrier", "off"};
        o += std::string(firstArc ? "" : ",") + "{\"x\":" + jsonNum(s.x) + ",\"end\":\"" + ends[int(f.end)] + "\",\"path\":[";
        for (size_t i = 0; i + 1 < f.path.size(); i += 2) o += std::string(i ? "," : "") + "[" + jsonNum(std::round(f.path[i] * 100) / 100) + "," + jsonNum(std::round(f.path[i + 1] * 100) / 100) + "]";
        o += "]}";
        firstArc = false;
    }
    o += "]";
    auto notes = [&](const std::vector<Note>& v) {
        std::string s = "[";
        for (size_t i = 0; i < v.size(); ++i) s += std::string(i ? "," : "") + "{\"x\":" + jsonNum(v[i].x) + ",\"y\":" + jsonNum(v[i].y) + ",\"text\":\"" + jsonEsc(v[i].text) + "\"}";
        return s + "]";
    };
    o += ",\"notes\":" + notes(rep.notes) + ",\"short\":" + notes(rep.short_) + ",\"doors\":" + notes(rep.doors) + ",\"walk\":[";
    for (size_t i = 0; i < rep.walk.size(); ++i) o += std::string(i ? "," : "") + "[" + jsonNum(rep.walk[i][0]) + "," + jsonNum(rep.walk[i][1]) + "]";
    o += "]";
    if (game) {
        const GameRun r = playLevel(d.number, cat, cancel);
        o += ",\"game\":{\"cleared\":" + std::string(r.cleared ? "true" : "false") + ",\"seconds\":" + jsonNum(r.seconds) + ",\"x\":" + jsonNum(r.x) + ",\"y\":" + jsonNum(r.y) +
             ",\"perfects\":" + std::to_string(r.perfects) + ",\"closeCalls\":" + std::to_string(r.closeCalls) + ",\"trace\":[";
        for (size_t i = 0; i < r.trace.size(); ++i) o += std::string(i ? "," : "") + "[" + jsonNum(r.trace[i][0]) + "," + jsonNum(r.trace[i][1]) + "," + jsonNum(r.trace[i][2]) + "]";
        o += "],\"events\":[";
        for (size_t i = 0; i < r.events.size(); ++i) o += std::string(i ? "," : "") + "[" + jsonNum(r.events[i][0]) + "," + jsonNum(r.events[i][1]) + "," + jsonNum(r.events[i][2]) + "]";
        o += "]}";
    }
    o += "}";
    return o;
}

namespace {

// a maze chunk set in its test corridor and judged as the chunk editor's checker judges it: the exit reached by stacking alone, one
// way through, no traps, out with the slow meter (near misses are shown, not asked for). `json` is the gauntlet report;
// `problem` is what fails, "" for nothing
struct Corridor { std::string json, problem; };
Corridor judgedCorridor(const Chunk& chunk, int level, const std::string& name) {
    Corridor out;
    LevelCatalog cat;
    std::string why;
    if (!cat.setGauntlet(name, chunkCorridor(chunk), &why)) {
        out.json = "{\"ok\":false,\"error\":\"" + jsonEsc(why) + "\"}";
        out.problem = "doesn't read in its corridor: " + why;
        return out;
    }
    const Gauntlet& g = *cat.gauntlet(name);
    out.json = gauntletReportJson(g, name, level, false, &cat);
    const GauntletReady r = gauntletReady(g, level, &cat);
    out.problem = !r.reachable ? "no way to the exit" : r.routes != 1 ? "more than one way through" : r.softLocks ? "a trap (a place she'd bounce forever)" : !r.meterOut ? "out of eggs with the slow meter" : "";
    return out;
}

} // namespace

std::string chunkReportJson(const std::string& id, const std::string& text, bool game) {
    std::string why;
    const std::optional<Chunk> c = parseChunk(id, text, &why);
    if (!c) return "{\"ok\":false,\"id\":\"" + jsonEsc(id) + "\",\"error\":\"" + jsonEsc(why) + "\"}";
    // (judged as a level of its own; the bot's run reads the level from a catalog that has it)
    auto judgedBody = [&](const LevelData& body) {
        if (!game) return levelReportJson(body, false);
        LevelCatalog cat;
        cat.setLevel(body);
        return levelReportJson(body, true, &cat);
    };
    std::string o = "{\"ok\":true,\"id\":\"" + jsonEsc(id) + "\",\"kind\":\"" + (c->maze ? "maze" : "course") + "\",\"ranges\":[";
    for (size_t i = 0; i < c->ranges.size(); ++i) o += std::string(i ? "," : "") + "[" + std::to_string(c->ranges[i].from) + "," + std::to_string(c->ranges[i].to) + "]";
    o += "],\"weight\":" + std::to_string(c->weight) + ",\"uses\":" + std::to_string(c->uses) + ",\"mirror\":" + (c->mirror ? "true" : "false") +
         ",\"mirrors\":" + (c->mirrors() ? "true" : "false");
    std::string whyNot;
    if (c->mirror && !c->canMirror(&whyNot)) o += ",\"mirrorNote\":\"" + jsonEsc(whyNot) + "\"";
    if (!c->maze) {
        o += ",\"report\":" + judgedBody(c->body);
        if (c->mirrors()) o += ",\"flipped\":" + judgedBody(mirrored(*c).body);
        return o + "}";
    }
    // a maze chunk: judged in a plain corridor, flipped too if Mirror is on and it can be; one that can't be played is turned down
    // ("ok":false, "error": why) so the Mac refuses it on a push
    const int level = c->ranges.empty() ? 1 : c->ranges.front().from;
    const Corridor plain = judgedCorridor(*c, level, "chunk_corridor");
    o += ",\"report\":" + plain.json;
    std::string problem = plain.problem;
    if (c->mirrors()) {
        const Corridor flip = judgedCorridor(mirrored(*c), level, "chunk_corridor_flipped");
        o += ",\"flipped\":" + flip.json;
        if (problem.empty() && !flip.problem.empty()) problem = "flipped: " + flip.problem;
    }
    if (problem.empty()) return o + "}";
    o.replace(0, 11, "{\"ok\":false,"); // ("{"ok":true," -> "{"ok":false,")
    return o + ",\"error\":\"" + jsonEsc("unplayable in a corridor: " + problem) + "\"}";
}

std::string levelReportText(const LevelData& d, const LevelCatalog* cat) {
    std::string o;
    char b[512];
    auto line = [&](const char* s) { o += s; };
    Level l;
    l.load(d, cat);
    l.generateUntil(d.length + 20);
    const Report rep = judged(l, d);
    std::snprintf(b, sizeof b, "level %d (%s): %.0f m, %d walls (the tallest %d), %d barriers, %d floating blocks, %zu saws and blades, %zu disco balls, %zu gauntlet(s)\n", d.number,
                zoneName(d.number), d.length, rep.walls, rep.tallest, rep.barriers, rep.blocks, l.saws.size(), l.corns.size(), l.gauntlets.size());
    line(b);
    std::snprintf(b, sizeof b, "  the egg budget: %s (the least a careful player holds: %.1f)\n", rep.short_.empty() ? "every climb and blade affordable" : "SHORT", rep.lowest);
    line(b);
    for (const Note& n : rep.short_) { std::snprintf(b, sizeof b, "    %6.1f m: %s\n", n.x, n.text.c_str()); line(b); }
    for (const Note& n : rep.doors) { std::snprintf(b, sizeof b, "  DOOR, %6.1f m: %s\n", n.x, n.text.c_str()); line(b); }
    for (const Note& n : rep.notes) { std::snprintf(b, sizeof b, "  note, %6.1f m: %s\n", n.x, n.text.c_str()); line(b); }
    return o;
}

std::string courseRulesJson() {
    std::string byLevel;
    int longest = 0;
    for (int n = 1; n <= 100; ++n) {
        Level l;
        l.setDifficulty(35.0 * (n - 1), 0.35);
        if (const LevelData* d = levelData(n)) l.setDifficulty(d->diffOffset, d->diffScale);
        const int fore = l.forecourtAt(Campaign::gauntletAt(n));
        longest = std::max(longest, fore);
        byLevel += (n > 1 ? "," : "") + std::to_string(fore);
    }
    const std::string registry = registryJson(); // ({"hazards":[...],"kinds":[...]}: its insides follow)
    return "{\"cell\":" + jsonNum(U) + ",\"lintel\":" + std::to_string(Level::kLintel) + ",\"porch\":" + std::to_string(Level::kPorch) +
           ",\"bodyRows\":" + std::to_string(Gauntlet::kBodyRows) + ",\"budgetCap\":" + jsonNum(Level::kBudgetCap) +
           ",\"forecourt\":" + std::to_string(longest) + ",\"forecourtByLevel\":[" + byLevel + "]," + registry.substr(1);
}

} // namespace cs
