// Prints the C++ course in the same format as level_parity.mjs, for a byte-for-byte diff.
#include "../src/core/Level.h"
#include <cstdio>
#include <cstdlib>
#include <string>
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    cs::Level level;
    level.reset(cs::seedFor(std::string("cluckstack:") + argv[1]));
    level.generateUntil(std::atof(argv[2]));
    const char* kinds[] = {"crate", "hay", "mix"};
    for (const auto& s : level.segs) std::printf("S %.9f %.9f %d %s %d\n", s.x0, s.x1, s.h, kinds[int(s.kind)], s.ceil);
    for (const auto& c : level.corns) std::printf("C %.9f %.9f\n", c.x, c.y);
}
