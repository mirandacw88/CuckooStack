// The hazards' registry (Hazards.h): one row is all a hazard takes. A "beacon" the game doesn't have, registered here,
// reads from its section in a level file and writes back, is laid into the course by its own spawn, gets its notes and
// its egg cost from the checker, and is in the registry the level editors read, with nothing else changed. A section
// this build doesn't know is kept as it is. The game's own wall lasers and lava read, write back and are laid and
// judged. And every level the game ships reads and writes back byte for byte.
#include "TestUtil.h"
#include "core/Hazards.h"
#include "core/LevelData.h"
#include "core/LevelJudge.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace cs;
using test::check;

namespace {

int beaconsLaid = 0;

const HazardType kBeacon = {
    "beacon", "beacons", "a beacon: where along, how high (eggs), how long (metres)",
    {{"x", "Along", "m", 0, 5000, 0.1, 0}, {"y", "Beam", "eggs", 1, 20, 1, 3}, {"len", "Length", "m", 0.5, 20, 0.1, 2}},
    1, false,
    "Beacon", "beacons", "q", "A light over the course at a height.",
    "rays", "marker", 0xff2b4a, Anchor::Free, 0, 0, true,
    [](const Placed& p, const LevelData& d) { return p.x() > d.length ? std::string("a beacon past the finish line") : std::string(); },
    [](Level& l, const Placed& p) { ++beaconsLaid; l.addCorn(p.x(), p.at(1) * U); }, // (stands in for the game's own: a marker on the course)
    [](const JudgeView& v, Report& r) { r.notes.push_back({v.l.finish(), 0, "a beacon note"}); },
    [](const Level& l, std::vector<EggCost>& out) { (void)l; out.push_back({12, 0, 9, "A beacon"}); },
};

const char* kWithBeacon = "length 30\ndifficulty 70 0.35\nraids auto\ncourse\n60 0 crate\nsaws\n20\ndiscos\nbeacons\n12 3 2.5\n18.2 4 1\n";

void aRowIsAllItTakes() {
    std::string why;
    registerHazard(&kBeacon);
    const auto d = parseLevel(4, kWithBeacon, &why);
    check(d.has_value(), "registered: a beacon's section reads");
    if (!d) { std::printf("  %s\n", why.c_str()); unregisterHazard(&kBeacon); return; }
    check(d->countOf(kBeacon) == 2 && d->items.back().at(2) == 1 && d->unknown.empty(), "...each its numbers");
    check(levelText(*d) == kWithBeacon, "...and writes back as it was");
    check(!parseLevel(4, "length 30\ncourse\n60 0 crate\nbeacons\n12 30 2\n", &why) && why.find("a beacon: ") != std::string::npos, "a beam out of its range: the row's own words");
    check(!parseLevel(4, "length 30\ncourse\n60 0 crate\nbeacons\n40 3 2\n", &why) && why.find("past the finish") != std::string::npos, "...its own validation");
    beaconsLaid = 0;
    Level l;
    l.load(*d);
    l.generateUntil(d->length + 20);
    check(beaconsLaid == 2, "the course lays them, each by its row's spawn");
    const Report r = judge(l);
    bool noted = false, short_ = false;
    for (const Note& n : r.notes) noted = noted || n.text == "a beacon note";
    for (const Note& n : r.short_) short_ = short_ || n.text.find("A beacon needs 9 eggs") != std::string::npos;
    check(noted && short_, "the checker: its notes, and its cost in the egg budget");
    const std::string json = registryJson();
    check(json.find("\"id\":\"beacon\"") != std::string::npos && json.find("\"key\":\"len\"") != std::string::npos && json.find("\"icon\":\"rays\"") != std::string::npos,
          "the registry the level editors read has it: its tool, its numbers");
    unregisterHazard(&kBeacon);
    check(registryJson().find("\"id\":\"beacon\"") == std::string::npos, "...and not once it's gone");
}

void unknownSectionsKept() {
    std::string why;
    const auto d = parseLevel(4, kWithBeacon, &why);
    check(d.has_value() && d->unknown.size() == 3 && d->unknown[0] == "beacons", "a section this build doesn't know: kept, line by line");
    check(d && levelText(*d) == kWithBeacon, "...written back after the others, as it was");
    check(d && d->countOf(kSawHazard) == 1 && d->countOf(kDiscoHazard) == 0, "...the rest read as ever");
    check(!parseLevel(4, "length 30\ncourse\n60 0 crate\nsaws\nx\n", &why), "a word too short for a section: what it says it is (a bad saw)");
}

// the game's wall lasers and lava: their sections read and write back; laid into the course (a laser in a wall's side
// at its row, its reach out along it to what stops it; lava along its cells); their costs in the egg budget
void lasersAndLava() {
    const char* text = "length 36\ndifficulty 70 0.35\nraids auto\ncourse\n20 0 crate\n20 2 crate\n20 0 crate\nsaws\ndiscos\nlasers\n12 1 0\n24 0 1\nlava\n30 3\n";
    std::string why;
    const auto d = parseLevel(4, text, &why);
    check(d.has_value(), ("a level with lasers and lava reads " + why).c_str());
    if (!d) return;
    check(d->countOf(kLaserHazard) == 2 && d->countOf(kLavaHazard) == 1, "...each its numbers");
    check(levelText(*d) == text, "...and writes back as it was");
    check(!parseLevel(4, "length 36\ncourse\n20 0 crate\n20 2 crate\n20 0 crate\nlasers\n12.3 1 0\n", &why), "a laser off a column's edge: turned away");
    check(!parseLevel(4, "length 36\ncourse\n20 0 crate\n20 2 crate\n20 0 crate\nlasers\n6 1 0\n", &why) && why.find("set into") != std::string::npos, ("...one with no wall's side to be set into: " + why).c_str());
    check(!parseLevel(4, "length 36\ncourse\n20 0 crate\n20 2 crate\n20 0 crate\nlasers\n12 1 1\n", &why), "...one firing into its own wall");
    check(!parseLevel(4, "length 30\ncourse\n60 0 crate\nlava\n10 9\n", &why), "lava longer than 8 cells: turned away");
    check(!parseLevel(4, "length 18\ncourse\n30 0 crate\nlava\n17 3\n", &why) && why.find("past the course") != std::string::npos, "...lava past the course's end");
    Level l;
    l.load(*d);
    l.generateUntil(d->length + 30);
    check(l.lasers.size() == 2 && l.lasers[0].row == 1 && l.lasers[0].dir == -1 && l.lasers[1].dir == 1, "the course lays the lasers: their rows, the way each fires");
    Laser a = l.lasers[0], b = l.lasers[1];
    check(l.traceLaser(a) && std::abs(a.x0) < 1e-9 && std::abs(a.x1 - 12) < 1e-9, "...one's reach back along the street to the course's start");
    check(l.traceLaser(b) && std::abs(b.x0 - 24) < 1e-9 && std::abs(b.x1 - (24 + 26 * U)) < 1e-6, "...the other's on from its face, 16 m at the most (26 cells)");
    check(l.lavas.size() == 1 && l.lavas[0].cells() == 3 && l.lavas[0].floor == 0, "...and the lava, along its cells");
    std::vector<EggCost> costs;
    kLaserHazard.costs(l, costs);
    kLavaHazard.costs(l, costs);
    double eggs = 0;
    for (const EggCost& c : costs) eggs += c.eggs;
    check(costs.size() == 3 && eggs == 5, "their costs: a laser she can't pass short an egg (at her body; dropped through), lava an egg a cell");
    const std::string json = registryJson();
    check(json.find("\"id\":\"laser\"") != std::string::npos && json.find("\"anchor\":\"face\"") != std::string::npos &&
          json.find("\"id\":\"lava\"") != std::string::npos && json.find("\"spanParam\":1") != std::string::npos,
          "the registry the level editors read has them: in a face, a stretch");
}

// a laser's reach and what it costs, laid into a course: its beam stops at the first solid cell of its row (a wall, a
// floating block, a barrier), 26 cells at the most; it takes an egg only where she can't pass it short (at her body
// with no eggs, dropped through from a wall, stacked up through to climb a wall), never for a block she can duck under
void laserReach() {
    const char* text = "length 40\ncourse\n10 0 crate\n2 0 crate 2 4\n10 0 crate\n4 0 crate 3\n14 0 crate\n2 6 crate\n2 0 crate 5 7\n30 0 crate\n"
                       "lasers\n7.2 3 1\n24 1 0\n26.4 6 1\n";
    std::string why;
    const auto d = parseLevel(4, text, &why);
    check(d.has_value(), ("lasers in a block's side, a wall's side: they read " + why).c_str());
    if (!d) return;
    check(!parseLevel(4, std::string(text) + "25.2 5 1\n", &why) && why.find("fires into a wall") != std::string::npos, "...one firing straight into a block: turned away");
    Level l;
    l.load(*d);
    l.generateUntil(d->length + 30);
    check(l.solidAt(10.5 * U, 2) && !l.solidAt(10.5 * U, 1) && l.solidAt(23.5 * U, 3) && !l.solidAt(23.5 * U, 2) && l.solidAt(40.5 * U, 5) && !l.solidAt(30 * U, 0),
          "the course's cells: a block's, a barrier's from its underside up, a wall's; the street open");
    Laser a = l.lasers[0], b = l.lasers[1], c = l.lasers[2];
    check(l.traceLaser(a) && std::abs(a.x0 - 12 * U) < 1e-9 && std::abs(a.x1 - 22 * U) < 1e-9, "its beam stops at a barrier in its row");
    check(l.traceLaser(b) && std::abs(b.x0 - 14 * U) < 1e-9 && std::abs(b.x1 - 40 * U) < 1e-9, "...goes on under a block over its row, and stops at 26 cells");
    check(l.traceLaser(c) && std::abs(c.x0 - 44 * U) < 1e-9 && std::abs(c.x1 - 70 * U) < 1e-9, "...from a block's right side, on along the street");
    check(!l.laserTakesAnEgg(a) && l.laserTakesAnEgg(b) && !l.laserTakesAnEgg(c),
          "an egg: for the one at her body over the street, none for the ones over her head (a block's, ducked under)");
    std::vector<EggCost> costs;
    kLaserHazard.costs(l, costs);
    check(costs.size() == 1 && std::abs(costs[0].x - 14 * U) < 1e-9, "...charged in the budget where she comes into its beam");
    Level fresh;
    Laser ahead;
    ahead.x = 6; ahead.row = 0; ahead.dir = 1;
    check(!fresh.traceLaser(ahead) && !ahead.traced(), "one firing on along a course not laid that far yet: not traced yet");
    // the checker's notes: one whose beam reaches the start at her body; one firing into a wall (laid by hand)
    const auto start = parseLevel(4, "length 20\ncourse\n10 0 crate\n2 3 crate\n30 0 crate\nlasers\n6 1 0\n", &why);
    check(start.has_value(), ("a laser back along the street to the start " + why).c_str());
    if (!start) return;
    Level s;
    s.load(*start);
    s.generateUntil(60);
    s.addLaser(6.6, 0, 1); // (between the wall's two cells: firing into the next)
    bool atStart = false, nowhere = false;
    for (const auto& n : judge(s).notes) {
        atStart = atStart || n.text.find("reaches the level's start") != std::string::npos;
        nowhere = nowhere || n.text.find("goes nowhere") != std::string::npos;
    }
    check(atStart && nowhere, "the checker: a beam reaching the start at her body, one firing into a wall");
}

// the editors' tool groups: the dangers in the Hazards flyout, the disco ball (good for her) its own button
void toolGroups() {
    check(std::string(kSawHazard.group) == "hazards" && std::string(kLaserHazard.group) == "hazards" && std::string(kLavaHazard.group) == "hazards",
          "saws, lasers and lava: the Hazards group");
    check(std::string(kDiscoHazard.group) == "pickups", "...the disco ball a pickup");
    const std::string json = registryJson();
    check(json.find("\"group\":\"pickups\"") != std::string::npos && json.find("\"group\":\"hazards\"") != std::string::npos, "...in the registry the editors read");
}

void everyShippedLevelByteForByte() {
    int n = 0, same = 0;
    for (const auto& e : std::filesystem::directory_iterator(std::string(CS_SOURCE_DIR) + "/assets/levels")) {
        const std::string name = e.path().filename().string();
        if (name.rfind("level_", 0) != 0) continue;
        std::ifstream in(e.path());
        std::stringstream text;
        text << in.rdbuf();
        const auto d = parseLevel(std::atoi(name.c_str() + 6), text.str());
        ++n;
        same += d && levelText(*d) == text.str();
    }
    check(n >= 30 && same == n, "every level the game ships writes back byte for byte");
}

void wallKindsFromTheRegistry() {
    check(wallKinds().size() == 6 && wallKindNamed("hay")->kind == WallKind::Hay && std::string(wallKindInfo(WallKind::Mix).id) == "mix", "the walls' kinds: the registry's");
    check(wallKindNamed("bumper")->bouncer && wallKindNamed("gate>")->kind == WallKind::GateR && wallKindNamed("gate<")->kind == WallKind::GateL && !wallKindNamed("mix")->bouncer,
          "...the bouncers among them: a bumper, a gate each way");
    check(registryJson().find("\"id\":\"bumper\",\"label\":\"Bumper\"") != std::string::npos && registryJson().find("\"bouncer\":true") != std::string::npos,
          "...in the editors' registry, marked");
    check(registryJson().find("\"kinds\":[{\"id\":\"crate\"") != std::string::npos, "...in the registry the editors read");
}

} // namespace

int main() {
    aRowIsAllItTakes();
    unknownSectionsKept();
    lasersAndLava();
    laserReach();
    toolGroups();
    everyShippedLevelByteForByte();
    wallKindsFromTheRegistry();
    return test::finish("Hazards");
}
