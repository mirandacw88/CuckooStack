#include "BotRun.h"

#include "Campaign.h"
#include "Game.h"
#include "Level.h"
#include "MemoryServices.h"

namespace cs {

GameRun playLevel(int level, const LevelCatalog* cat, const std::atomic<bool>* cancel) {
    GameRun r;
    MemoryStorage st = MemoryStorage::ready();
    FixedClock clock;
    Campaign(&st, cat).debugSetLevel(level);
    GameServices svc;
    svc.storage = &st; svc.clock = &clock; svc.levels = cat; // (no ads: none ready)
    Game g(svc);
    g.debugSeed(1);
    g.setHeadless(true);
    g.debugSetBot({true, false, false, 0});
    g.press();
    int frames = 0, perfects = 0, calls = 0;
    for (; frames < 60 * 180 && g.state() == Game::State::Play && !(cancel && *cancel); ++frames) {
        g.update(1.0 / 60.0);
        const float x = g.hen().nodes[size_t(g.hen().root)].pos.x, y = g.henBase();
        if (frames % 5 == 0) r.trace.push_back({x, y, float(g.eggCount())});
        if (g.perfects() > perfects) r.events.push_back({x, y + g.eggCount() * float(U), 0});
        if (g.closeCalls() > calls) r.events.push_back({x, y + g.eggCount() * float(U), 1});
        perfects = g.perfects(); calls = g.closeCalls();
    }
    r.cleared = g.lastRunCleared();
    if (!r.cleared && g.state() == Game::State::Dead) r.death = int(g.death());
    r.seconds = frames / 60.0;
    r.perfects = perfects; r.closeCalls = calls;
    r.x = g.hen().nodes[size_t(g.hen().root)].pos.x;
    r.y = g.henBase();
    return r;
}

} // namespace cs
