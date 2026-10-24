// The disco ball (Level.h's Corn; the game's GameSurge.cpp): grabbed by the top of her stack, +5 points and an egg and
// a half for the meter.
#include "../Hazards.h"
#include "../LevelData.h"

namespace cs {

namespace {

std::string validate(const Placed& p, const LevelData& d) {
    double reach = 0;
    for (const LevelData::Wall& w : d.course) reach += w.cells * U;
    if (p.x() > reach) return "a disco ball at " + levelNum(p.x()) + " m is past the course's end";
    return "";
}

void spawn(Level& l, const Placed& p) { l.addCorn(p.x(), p.at(1)); }

} // namespace

const HazardType kDiscoHazard = {
    "disco", "discos", "a disco ball: where along, how high (metres), one a line",
    {{"x", "Along", "m", 0, 5000, 0.1, 0}, {"y", "Height", "m", 0, 40, 0.05, 1.45}},
    1, true,
    "Disco ball", "disco balls", "d",
    "Grabbed by the top of her stack: +5 points, and an egg and a half for the meter.",
    "circle.hexagongrid.fill", "disco", 0xf4ff5a, Anchor::Free, 0, 0, false,
    validate, spawn, nullptr, nullptr,
    -1, true, "pickups",
};

} // namespace cs
