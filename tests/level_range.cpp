// The generators' expressive range (Smith and Whitehead, PCG 2010; LevelBeats.h's traits): for each level and seed,
// a course from the rhythm-beat generator (or the classic one), and what it's like (its linearity, leniency and pacing,
// its tallest wall, its pieces), a JSON line each. scripts/levels/expressive_range.py draws them.
//
//   level_range [--from 31] [--to 50] [--seeds 10] [--classic]
#include "core/Campaign.h"
#include "core/LevelBake.h"
#include "core/LevelBeats.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace cs;

int main(int argc, char** argv) {
    int from = 31, to = 50, seeds = 10;
    bool classic = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--from" && i + 1 < argc) from = std::atoi(argv[++i]);
        else if (a == "--to" && i + 1 < argc) to = std::atoi(argv[++i]);
        else if (a == "--seeds" && i + 1 < argc) seeds = std::atoi(argv[++i]);
        else if (a == "--classic") classic = true;
        else { std::fprintf(stderr, "level_range [--from N] [--to N] [--seeds N] [--classic]\n"); return 2; }
    }
    for (int n = from; n <= to; ++n)
        for (int k = 0; k < seeds; ++k) {
            const uint32_t seed = Campaign::seedOf(n) + uint32_t(k) * 7919u;
            std::string pieces;
            LevelData d;
            if (classic) d = bakeClassic(n, nullptr, seed);
            else {
                const beats::Plan p = beats::compose(n, seed);
                d = p.data;
                for (const beats::Group& g : p.groups) pieces += (pieces.empty() ? "" : " ") + g.pieces;
            }
            const beats::Traits t = beats::traits(d);
            std::printf("{\"level\":%d,\"seed\":%u,\"generator\":\"%s\",\"linearity\":%.4f,\"leniency\":%.4f,\"pacing\":%.3f,\"tallest\":%d,\"pieces\":\"%s\"}\n",
                        n, seed, classic ? "classic" : "beats", t.linearity, t.leniency, t.pacing, t.tallest, pieces.c_str());
            std::fflush(stdout);
        }
}
