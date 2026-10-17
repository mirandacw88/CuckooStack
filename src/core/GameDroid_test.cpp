// The jet droid (GameDroid.cpp). On the title it circles the hen (behind her and in front), nose first, its fans
// spinning and its flames lit, with a shadow on the street right below it; a run sends it boosting off screen, and
// the Locker's close-up of the hen leaves it out. In a run it raids the stack: it warps in, circles high over the hen,
// and zaps the bottom egg (or two, one blue laser each), with its sounds; never when she carries fewer than 2 eggs.
#include "TestUtil.h"
#include "core/DroidModel.h"
#include "core/Campaign.h"
#include "core/Game.h"
#include "core/Level.h"

#include <cmath>
#include <map>
#include <optional>
#include <string>

using namespace cs;
using namespace test;

namespace {

struct RecordingAudio : IAudio {
    std::vector<std::pair<Sfx, int>> played;
    void play(Sfx s, int v) override { played.push_back({s, v}); }
    void musicStart(bool) override {}
    void musicStop(bool) override {}
    void musicTempo(float) override {}
    void musicSurge(bool) override {}
    void setMuted(bool) override {}
    int track = 0;
    void musicTrack(int t) override { track = t; }
    int count(Sfx s) const { int n = 0; for (auto& p : played) n += p.first == s; return n; }
};

struct Env {
    FakeStorage st = FakeStorage::ready();
    FakeClock clock;
    FakeAds ads;
    RecordingAudio audio;
    GameServices svc() { GameServices s; s.storage = &st; s.clock = &clock; s.ads = &ads; s.audio = &audio; return s; }
};

const std::vector<Instance>& bucket(const Game& g, Pass p, MeshId m) { return g.renderList().buckets[size_t(p)][size_t(m)]; }
glm::vec3 at(const Instance& i) { return glm::vec3(i.model[3]); }

float henX(const Game& g) { return g.hen().nodes[size_t(g.hen().root)].pos.x; }

// stacks n eggs (early in a run, on the flat run-up)
void stackUp(Game& g, int n) {
    for (int i = 0; i < n; ++i) { g.press(); step(g, 0.12f); }
    step(g, 0.3f);
}

// the laser beams on screen: each is three long boxes in the additive pass (a core, a body, a haze), the thinnest
// still thicker than the charging sight line
int beams(const Game& g) {
    int n = 0;
    for (const Instance& b : bucket(g, Pass::UnlitAdd, MeshId::Box)) n += glm::length(glm::vec3(b.model[0])) > 1.5f && glm::length(glm::vec3(b.model[1])) > 0.07f;
    return n / 3;
}
// the charging sight line: one long, very thin box
bool sightLine(const Game& g) {
    for (const Instance& b : bucket(g, Pass::UnlitAdd, MeshId::Box))
        if (glm::length(glm::vec3(b.model[0])) > 1.5f && glm::length(glm::vec3(b.model[1])) < 0.07f) return true;
    return false;
}


void circles() {
    Env env;
    Game g(env.svc());
    step(g, 0.2f);
    check(g.state() == Game::State::Title && g.droidVisible(), "the droid is on the title screen");
    check(bucket(g, Pass::Lit, MeshId::DroidBody).size() == 1 && bucket(g, Pass::Lit, MeshId::DroidFanL).size() == 1 &&
              bucket(g, Pass::Lit, MeshId::DroidFanR).size() == 1 && bucket(g, Pass::UnlitAdd, MeshId::DroidFlame).size() == 4,
          "drawn whole: body, both fans, and a flame and a core at each exhaust");
    check(bucket(g, Pass::Lit, MeshId::DroidBody)[0].params.z == float(Pattern::DroidTexture), "the body wears its own texture");

    // a lap: in front of the hen and behind her, on both sides of her, always above the street
    const float hen = g.hen().nodes[size_t(g.hen().root)].pos.x;
    float minZ = 1e9f, maxZ = -1e9f, minX = 1e9f, maxX = -1e9f, minY = 1e9f;
    glm::vec3 prev = g.droidPosition();
    bool smooth = true, shadowBelow = true;
    for (int i = 0; i < 60 * 8; ++i) {
        g.update(1.0 / 60.0);
        const glm::vec3 p = g.droidPosition();
        smooth = smooth && glm::length(p - prev) < 0.12f; // (no jumps: under 7 m/s)
        prev = p;
        minZ = std::min(minZ, p.z); maxZ = std::max(maxZ, p.z); minX = std::min(minX, p.x); maxX = std::max(maxX, p.x); minY = std::min(minY, p.y);
        if (i % 30 == 0) { // the shadow: a dark disc on the street, right below it
            bool found = false;
            for (const Instance& s : bucket(g, Pass::UnlitAlpha, MeshId::Plane)) {
                const glm::vec3 q = at(s);
                found = found || (std::abs(q.y) < 0.05f && std::abs(q.x - p.x) < 1e-3f && std::abs(q.z - p.z) < 1e-3f && s.color.r == 0.f && s.color.a > 0.2f);
            }
            shadowBelow = shadowBelow && found;
        }
    }
    check(minZ < -1.f && maxZ > 1.f, "it circles the hen: behind her and in front of her");
    check(minX < hen - 0.5f && maxX > hen + 2.f, "...passing her on both sides");
    check(minY > 1.f, "...always in the air");
    check(smooth, "...on a smooth path");
    check(shadowBelow, "its shadow follows it on the street");

    // nose first: its nose (the model's -x) points along the way it's flying
    const glm::vec3 a = g.droidPosition();
    step(g, 0.05f);
    const glm::vec3 b = g.droidPosition();
    const glm::mat4& m = bucket(g, Pass::Lit, MeshId::DroidBody)[0].model;
    const glm::vec3 nose = glm::normalize(glm::vec3(m * glm::vec4(-1, 0, 0, 0)));
    const glm::vec3 way = glm::normalize(glm::vec3(b.x - a.x, 0, b.z - a.z));
    check(glm::dot(glm::normalize(glm::vec3(nose.x, 0, nose.z)), way) > 0.6f, "it flies nose first (turned a little toward the camera)");

    // the fans spin: the same fan's orientation changes from one moment to the next
    const glm::mat4 f0 = bucket(g, Pass::Lit, MeshId::DroidFanL)[0].model;
    g.update(1.0 / 60.0);
    const glm::mat4 f1 = bucket(g, Pass::Lit, MeshId::DroidFanL)[0].model, b1 = bucket(g, Pass::Lit, MeshId::DroidBody)[0].model;
    const glm::mat3 rel0 = glm::mat3(glm::inverse(m) * f0), rel1 = glm::mat3(glm::inverse(b1) * f1); // fan relative to body
    check(glm::length(rel0[1] - rel1[1]) > 0.05f, "its fans spin");
}

void leaves() {
    Env env;
    Game g(env.svc());
    step(g, 0.5f);
    const glm::vec3 before = g.droidPosition();
    g.press(); // a run starts
    step(g, 0.5f);
    check(g.state() == Game::State::Play && g.droidVisible() && g.droidPosition().y > before.y + 0.3f,
          "a run starts: the droid peels off, climbing");
    step(g, 1.f);
    check(!g.droidVisible() && bucket(g, Pass::Lit, MeshId::DroidBody).empty(), "...and is gone");
}

void lockerHidesIt() {
    Env env;
    Game g(env.svc());
    step(g, 0.3f);
    g.debugOpenScreen("locker");
    step(g, 0.5f);
    check(!g.droidVisible() && bucket(g, Pass::Lit, MeshId::DroidBody).empty(), "the Locker's close-up of the hen leaves the droid out");
}

// Game_test's bot: reads the same course and stacks just enough eggs for the next wall (or keeps a few spare)
struct Bot {
    Level course;
    int spare = 0; // eggs to carry at least, over the floor (barriers allowing)
    float cooldown = 0;
    explicit Bot(IStorage* st) { Campaign(st).applyTo(course); course.generateUntil(2000); }
    void tick(Game& g, float dt) {
        cooldown -= dt;
        if (g.state() != Game::State::Play || cooldown > 0) return;
        const double x = henX(g), top = g.henHeight();
        double want = 0;
        for (const Segment& s : course.segs)
            if (s.x0 <= x && x < s.x1) want = s.h * U + spare * U;
        for (const Segment& s : course.segs) {
            if (s.x1 <= x + 0.22) continue;
            if (s.x0 > x + 6.5) break;
            want = std::max(want, s.h * U);
        }
        for (const Segment& s : course.segs)
            if (s.ceil && x + 0.25 >= s.x0 - 3 && x + 0.22 < s.x1) {
                const double safe = std::floor((s.ceil * U - HEN_H - 0.02) / U + 1e-6) * U;
                want = std::min(std::max(want, safe - U * 2), safe);
            }
        if (top + 0.05 < want) { g.press(); cooldown = 0.08f; }
    }
};

void raid(int shots) {
    // the bot plays level 1 (and re-stacks what the droid zaps); a raid is called as soon as it's allowed
    Env env;
    env.ads.state = RewardedState::Unavailable;
    Game g(env.svc());
    g.debugSeed(uint64_t(shots));
    Bot bot(&env.st);
    bot.spare = 3;
    step(g, 0.2f);
    const float titleScale = glm::length(glm::vec3(bucket(g, Pass::Lit, MeshId::DroidBody)[0].model[0]));
    float raidScale = 0;
    g.press();
    for (int i = 0; i < 60 * 60 && !(g.state() == Game::State::Play && g.eggCount() > shots && !g.droidVisible() && henX(g) > 15); ++i) {
        bot.tick(g, 1.f / 60);
        g.update(1.0 / 60.0);
    }
    check(g.state() == Game::State::Play && g.eggCount() > shots && !g.droidVisible(), "a run, eggs stacked, no droid yet");
    env.audio.played.clear();
    for (int i = 0; i < 60 * 30 && !g.droidRaiding() && g.state() == Game::State::Play; ++i) {
        g.debugDroidRaid(shots);
        if (g.droidRaiding()) break;
        bot.tick(g, 1.f / 60);
        g.update(1.0 / 60.0);
    }
    check(g.droidRaiding() && g.droidVisible() && env.audio.count(Sfx::DroidWarp) == 1, "the droid warps in (with its sound)");
    int maxBeams = 0, zapped = 0, lasers = 0, flashes = 0;
    int beamsBefore = 0;
    float thick = 0;
    glm::vec3 relPrev{0.f};
    bool inView = true, beamOnPrev = false, still = true, aimed = false, high = true, smooth = true, shadow = false;
    glm::vec3 prev = g.droidPosition();
    for (int i = 0; i < 60 * 4 && g.droidRaiding() && g.state() == Game::State::Play; ++i) {
        bot.tick(g, 1.f / 60);
        const int eggs = g.eggCount();
        g.update(1.0 / 60.0);
        if (env.audio.count(Sfx::DroidLaser) > lasers) {
            lasers = env.audio.count(Sfx::DroidLaser);
            zapped += eggs - g.eggCount() >= 1;
        }
        if (!g.droidRaiding()) break;
        if (g.droidShotsFired() == shots) bot.spare = 0; // (done: back to just enough)
        const glm::vec3 p = g.droidPosition();
        if (!bucket(g, Pass::Lit, MeshId::DroidBody).empty()) raidScale = glm::length(glm::vec3(bucket(g, Pass::Lit, MeshId::DroidBody)[0].model[0]));
        smooth = smooth && glm::length(p - prev) < 1.f; // (no jumps: under 60 m/s, even zipping off)
        prev = p;
        const int b = beams(g);
        if (b) { // while a beam is on, it holds still (relative to the hen, who keeps running)
            const glm::vec3 rel = p - glm::vec3(henX(g), 0, 0);
            if (beamOnPrev) still = still && glm::length(rel - relPrev) < 0.02f;
            inView = inView && rel.x > 0.5f && rel.z > 0.3f; // (ahead of her and toward the camera: on screen)
            relPrev = rel;
        }
        beamOnPrev = b > 0;
        flashes += std::max(0, b - beamsBefore);
        beamsBefore = b;
        aimed = aimed || (sightLine(g) && lasers == 0);
        if (b) thick = std::max(thick, [&] { float w = 0; for (const Instance& x : bucket(g, Pass::UnlitAdd, MeshId::Box)) w = std::max(w, glm::length(glm::vec3(x.model[1]))); return w; }());
        maxBeams = std::max(maxBeams, b);
        if (b) high = high && p.y > g.henHeight() + 1.8f;
        for (const Instance& s : bucket(g, Pass::UnlitAlpha, MeshId::Plane))
            shadow = shadow || (std::abs(at(s).x - p.x) < 1e-3f && std::abs(at(s).z - p.z) < 1e-3f && s.color.r == 0.f && at(s).y < p.y - 1.f);
    }
    check(smooth, "it flies on a smooth path");
    check(lasers == shots && zapped == shots, shots == 2 ? "two shots: two eggs gone" : "one shot: one egg gone");
    check(aimed, "before it fires, a sight line marks the egg it's aiming at");
    check(maxBeams >= 1 && flashes == shots, shots == 2 ? "...two lasers" : "...one laser");
    check(still, "...it holds still while it fires (the beam doesn't drift off its eye)");
    check(inView, "...from a spot in view: above her, a little ahead and toward the camera");
    check(std::abs(raidScale - titleScale) < 1e-3f, "it raids at the same size as on the title screen");
    check(thick > 0.5f, "...a big one: its glow over half a metre across");
    check(high, "it fires circling high over the hen");
    check(shadow, "its shadow follows it below");
    check(env.audio.count(Sfx::DroidCharge) == 1 && lasers == shots, "the laser charges and fires (sounds)");
    check(!g.droidRaiding() && !g.droidVisible() && g.state() == Game::State::Play, "then it zips off, and she runs on");
    for (int i = 0; i < 60 && g.state() == Game::State::Play; ++i) { bot.tick(g, 1.f / 60); g.update(1.0 / 60.0); }
    check(g.state() == Game::State::Play && bucket(g, Pass::Lit, MeshId::DroidBody).empty(), "...and it's not drawn any more");
}

void fromLevel5() {
    // levels 1-4: no raids; from level 5: one (its introduction), from level 7: one or two
    for (int level : {1, 2, 3, 4, 5, 6, 7, 8, 11, 15}) { // (11, 15: Neon Egypt too)
        size_t most = 0, least = 9;
        for (int run = 0; run < 12; ++run) {
            Env env;
            Campaign(&env.st).debugSetLevel(level);
            Game g(env.svc());
            g.debugSeed(uint64_t(run + 1));
            g.press();
            most = std::max(most, g.droidRaidPlan().size()); least = std::min(least, g.droidRaidPlan().size());
        }
        if (level < 5) check(most == 0, ("level " + std::to_string(level) + ": no droid raids").c_str());
        else if (level < 7) check(least == 1 && most == 1, ("level " + std::to_string(level) + ": one raid").c_str());
        else check(least >= 1 && most == 2, ("level " + std::to_string(level) + ": one or two raids").c_str());
    }
}

struct FakeRemote : IRemoteConfig {
    std::map<std::string, double> v;
    std::optional<double> number(const std::string& key) override {
        const auto it = v.find(key);
        return it == v.end() ? std::nullopt : std::optional<double>(it->second);
    }
};

void remoteConfig() {
    // Remote Config: droid_raids 0 switches the raids off; droid_level moves the level they start on
    auto plans = [](std::map<std::string, double> keys, int level) {
        size_t most = 0;
        for (int run = 0; run < 8; ++run) {
            Env env;
            FakeRemote rc;
            rc.v = keys;
            Campaign(&env.st).debugSetLevel(level);
            GameServices svc = env.svc();
            svc.remoteConfig = &rc;
            Game g(svc);
            g.debugSeed(uint64_t(run + 1));
            g.press();
            most = std::max(most, g.droidRaidPlan().size());
        }
        return most;
    };
    check(plans({}, 6) > 0 && plans({{"droid_raids", 0}}, 6) == 0, "droid_raids 0: no raids (level 6 has them otherwise)");
    check(plans({}, 3) == 0 && plans({{"droid_level", 3}}, 3) > 0 && plans({{"droid_level", 8}}, 6) == 0,
          "droid_level: raids start on that level instead of 5");
}

void needsTwoEggs() {
    {
        Env env;
        Game g(env.svc());
        g.press();
        step(g, 0.2f);
        stackUp(g, 1);
        g.debugDroidRaid(1);
        check(g.eggCount() == 1 && !g.droidRaiding(), "one egg under the hen: no raid");
    }
    // the bot's runs on level 1: the raids it plans fall due mid-course, the droid strikes only with 2 eggs or more,
    // and the course stays clearable
    int raids = 0, killed = 0, silent = 0;
    bool fair = true, midCourse = true;
    for (int run = 0; run < 6; ++run) {
        Env env;
        env.ads.state = RewardedState::Unavailable;
        Campaign(&env.st).debugSetLevel(5); // (its first level)
        Game g(env.svc());
        g.debugSeed(uint64_t(run + 1));
        Bot bot(&env.st);
        g.press();
        const double finish = bot.course.finish();
        for (double x : g.droidRaidPlan()) midCourse = midCourse && x > 0.15 * finish && x < 0.9 * finish;
        bool was = false;
        int eggsBefore = 0;
        float sinceZap = 99;
        int zaps = 0, lasersAtRaid = 0;
        for (int i = 0; i < 60 * 90 && g.state() == Game::State::Play; ++i) {
            bot.tick(g, 1.f / 60);
            eggsBefore = g.eggCount();
            g.update(1.0 / 60.0);
            if (g.droidRaiding() && !was) { ++raids; fair = fair && eggsBefore >= 2; lasersAtRaid = env.audio.count(Sfx::DroidLaser); }
            if (!g.droidRaiding() && was && g.state() == Game::State::Play && !g.surgeStatus().surging)
                silent += env.audio.count(Sfx::DroidLaser) == lasersAtRaid;
            was = g.droidRaiding();
            sinceZap = env.audio.count(Sfx::DroidLaser) > zaps ? 0.f : sinceZap + 1.f / 60;
            zaps = env.audio.count(Sfx::DroidLaser);
            killed += g.state() != Game::State::Play && !g.lastRunCleared() && sinceZap < 0.8f; // (a crash right after a zap: its fault)
        }
    }
    check(midCourse, "raids are planned mid-course (not at the start or the finish)");
    check(raids >= 3, "the droid raids the bot's runs");
    check(fair, "...only ever with 2 eggs or more under the hen");
    check(silent == 0, "...and it fires every time it comes (it waits for a safe moment, it doesn't give up)");
    check(killed == 0, "...and a zap never costs her the run (there's time to re-stack what it takes)");
}

void comesFromAllAround() {
    // its warp-in points: all around the hen (left, right, behind, in front) and from above
    bool left = false, right = false, back = false, front = false, above = false;
    for (int seed = 0; seed < 24; ++seed) {
        Env env;
        Game g(env.svc());
        g.debugSeed(uint64_t(seed + 1));
        g.press();
        step(g, 0.2f + 0.02f * seed);
        stackUp(g, 3);
        g.debugDroidRaid(1);
        const glm::vec3 d = g.droidPosition() - glm::vec3(g.hen().nodes[size_t(g.hen().root)].pos);
        left = left || d.x < -4; right = right || d.x > 4; back = back || d.z < -4; front = front || d.z > 4; above = above || d.y > 6;
    }
    check(left && right && back && front && above, "it comes in from every side");
}

} // namespace

int main() {
    if (!droidModel().ok) { check(false, "the droid model is embedded"); return finish("GameDroid"); }
    circles();
    leaves();
    lockerHidesIt();
    raid(1);
    raid(2);
    fromLevel5();
    remoteConfig();
    needsTwoEggs();
    comesFromAllAround();
    return finish("GameDroid");
}
