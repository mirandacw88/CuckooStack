// Rendering work added for the cyber-cube walls and the wet-street reflection, everything that runs without a GPU:
//  - the cyber cube bake (BakedPatterns.cpp): atlas layout, regions, glow channels, the four circuit layouts
//  - the reflection's mirroring (RenderList::litMark / reflectSince), one- and two-sided instances
//  - the wall materials the game emits (cyber cubes, neon crates: cool cyan edges, not amber)
//  - frame pacing (Quality.h FramePacer): an even 30 fps on a GPU that can't keep 60, back to 60 with headroom
//  - frame timing on the display's refresh grid (Quality.h nextFrameVsync, Android's pacing)
//  - the Android manifest: every configuration change handled in place (no window rework / restart mid-run)
//  - the profiling frame summary, and no glow pad under the hen
//  - a guard on the heat fix: only the reflection shader writes depth (writing it in lit.frag disables the GPU's
//    hidden-surface removal for every solid object)
// (The street reflection's game logic - on the street, mid-air, crash falls, never over walls - is in replay_test.)
#include "TestUtil.h"
#include "graphics/BakedPatterns.h"
#include "graphics/Quality.h"
#include "graphics/ShaderBlobs.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

using namespace cs;
using namespace test;

namespace {

// ---------------------------------------------------------------- cyber cube bake

void cyberBake() {
    constexpr int n = 64; // small: same maths as the game's 256
    const CyberCubeBake b = bakeCyberCube(n);
    const size_t W = 2 * n;
    check(b.surface.size() == W * W * 4 && b.glow.size() == W * W * 4, "bake: two RGBA atlases, 2 x 2 faces of n x n");
    auto at = [&](const std::vector<uint8_t>& img, int variant, float u, float v, int ch) {
        const int x = (variant % 2) * n + std::min(n - 1, int(u * n)), y = (variant / 2) * n + std::min(n - 1, int(v * n));
        return int(img[(size_t(y) * W + size_t(x)) * 4 + size_t(ch)]);
    };
    const int kFrame = 0, kBolt = 85, kPanel = 170, kTape = 255; // surface alpha: kind / 3
    bool regions = true, glowOk = true;
    for (int v = 0; v < 4; ++v) {
        regions = regions && at(b.surface, v, 0.02f, 0.5f, 3) == kFrame;          // the armour frame at the edge
        regions = regions && at(b.surface, v, 18.f / 512.f, 18.f / 512.f, 3) == kBolt; // a bolt in the corner
        regions = regions && at(b.surface, v, 0.3f, 0.3f, 3) == kPanel;           // the glass panel inside
        regions = regions && at(b.surface, v, 0.5f, 0.815f, 3) == kTape;          // the hazard tape at the bottom
        glowOk = glowOk && at(b.glow, v, 0.5f, 0.5f, 3) > 200;                    // the core's centre dot
        glowOk = glowOk && at(b.glow, v, 0.5f + 58.f / 512.f, 0.5f, 2) > 100;     // the core ring
        glowOk = glowOk && at(b.glow, v, 0.5f, 24.f / 512.f, 0) > 100;            // the neon tube line, near the top edge
        glowOk = glowOk && at(b.glow, v, 0.02f, 0.5f, 0) == 0 && at(b.glow, v, 0.02f, 0.5f, 1) == 0; // nothing on the frame
    }
    check(regions, "bake: frame, bolts, glass panel and hazard tape where the design puts them, in every layout");
    check(glowOk, "bake: tube, core ring and centre dot glow; the frame doesn't");
    // the four layouts differ in their circuits (and circuits only glow inside the panel)
    std::set<std::vector<uint8_t>> layouts;
    bool circuitsInPanel = true;
    for (int v = 0; v < 4; ++v) {
        std::vector<uint8_t> c;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                const int g = at(b.glow, v, (x + 0.5f) / n, (y + 0.5f) / n, 1);
                c.push_back(uint8_t(g));
                // (texels on the panel's border may catch a little: the bake is anti-aliased; never on the frame)
                if (g > 0 && at(b.surface, v, (x + 0.5f) / n, (y + 0.5f) / n, 3) == kFrame) circuitsInPanel = false;
            }
        layouts.insert(c);
    }
    check(layouts.size() == 4, "bake: four different circuit layouts");
    check(circuitsInPanel, "bake: circuit traces stay off the armour frame");
    // colours are stored as sqrt(linear): the dark panel stays dark, the amber tape is amber
    const int pr = at(b.surface, 0, 0.3f, 0.3f, 0), pb = at(b.surface, 0, 0.3f, 0.3f, 2);
    check(pr < 40 && pb < 50, "bake: the glass panel is dark");
    bool amber = false;
    for (float u = 0.4f; u < 0.6f; u += 0.01f)
        amber = amber || (at(b.surface, 0, u, 0.815f, 0) > 200 && at(b.surface, 0, u, 0.815f, 2) < 100);
    check(amber, "bake: the hazard tape has amber stripes");
    check(bakeCyberCube(n).surface == b.surface, "bake: deterministic (same atlas every launch)");
}

// ---------------------------------------------------------------- the reflection's mirroring

void mirroring() {
    RenderList list;
    list.add(Pass::Lit, MeshId::Box).model = glm::translate(glm::mat4(1.f), glm::vec3(1, 2, 3)); // before the mark
    const auto mark = list.litMark();
    list.add(Pass::Lit, MeshId::Egg).model = glm::translate(glm::mat4(1.f), glm::vec3(4, 1.5f, -1));
    Instance& tinted = list.add(Pass::Lit, MeshId::Egg);
    tinted.model = glm::translate(glm::mat4(1.f), glm::vec3(0, 3, 0));
    tinted.color = glm::vec4(0.2f, 0.4f, 0.6f, 1.f);
    list.add(Pass::LitTwoSided, MeshId::HenBody).model = glm::translate(glm::mat4(1.f), glm::vec3(0, 0.8f, 0)); // shards
    list.add(Pass::UnlitAdd, MeshId::Plane);                                                                     // not lit
    list.reflectSince(mark);
    const auto& refl = list.buckets[size_t(Pass::Reflect)];
    check(refl[size_t(MeshId::Box)].empty(), "mirror: instances added before the mark aren't reflected");
    check(refl[size_t(MeshId::Egg)].size() == 2, "mirror: lit instances since the mark are reflected");
    check(refl[size_t(MeshId::HenBody)].size() == 1, "mirror: two-sided instances too (the crash's glass shards)");
    check(refl[size_t(MeshId::Plane)].empty(), "mirror: unlit effects aren't");
    const glm::vec4 p = refl[size_t(MeshId::Egg)][0].model * glm::vec4(0, 0, 0, 1);
    check(std::abs(p.x - 4) < 1e-5f && std::abs(p.y + 1.5f) < 1e-5f && std::abs(p.z + 1) < 1e-5f, "mirror: flipped about the street (y -> -y)");
    const glm::vec4 up = refl[size_t(MeshId::Egg)][1].model * glm::vec4(0, 1, 0, 0);
    check(up.y < 0, "mirror: upside down");
    check(refl[size_t(MeshId::Egg)][1].color == tinted.color, "mirror: the material is kept");
    check(list.buckets[size_t(Pass::Lit)][size_t(MeshId::Egg)].size() == 2, "mirror: the originals are untouched");
    list.clear();
    check(list.buckets[size_t(Pass::Reflect)][size_t(MeshId::Egg)].empty(), "mirror: cleared with the frame");
}

// ---------------------------------------------------------------- wall materials

void wallMaterials() {
    FakeStorage st = FakeStorage::ready();
    FakeClock clock;
    FakeAds ads;
    ads.state = RewardedState::Unavailable;
    GameServices s;
    s.storage = &st; s.clock = &clock; s.ads = &ads;
    Game g(s);
    g.press();
    step(g, 0.1f);
    g.debugStartSurge(); // invulnerable: runs past walls of both kinds
    int cubes = 0, crates = 0;
    bool coolEdges = true;
    for (int i = 0; i < 60 * 6 && g.state() == Game::State::Play; ++i) {
        g.update(1.0 / 60.0);
        for (const Instance& in : g.renderList().buckets[size_t(Pass::Lit)][size_t(MeshId::Box)]) {
            const int pattern = int(in.params.z + 0.5f);
            if (pattern != int(Pattern::Hazard) && pattern != int(Pattern::Container)) continue;
            (pattern == int(Pattern::Hazard) ? cubes : crates)++;
            coolEdges = coolEdges && in.rim.b > in.rim.r && in.rim.w <= 0.3f; // cyan, soft (was a strong amber)
        }
    }
    check(cubes > 0 && crates > 0, "walls: cyber cubes and neon crates are drawn");
    check(coolEdges, "walls: soft cyan edge glow on both kinds (no amber)");
}

// ---------------------------------------------------------------- heat guard: who writes depth

// SPIR-V: does the module decorate a variable as BuiltIn FragDepth (OpDecorate %id BuiltIn FragDepth)?
bool writesDepth(const uint32_t* words, size_t bytes) {
    const size_t n = bytes / 4;
    for (size_t i = 5; i < n;) {
        const uint32_t op = words[i] & 0xffff, len = words[i] >> 16;
        if (len == 0) break;
        if (op == 71 /* OpDecorate */ && len >= 4 && words[i + 2] == 11 /* BuiltIn */ && words[i + 3] == 22 /* FragDepth */) return true;
        i += len;
    }
    return false;
}

void depthGuard() {
    check(spv::lit_frag_size > 20 && spv::lit_frag[0] == 0x07230203, "shaders: lit.frag is SPIR-V");
    check(spv::lit_reflect_frag_size > 20 && spv::lit_reflect_frag[0] == 0x07230203, "shaders: lit_reflect.frag is SPIR-V");
    check(!writesDepth(spv::lit_frag, spv::lit_frag_size), "heat: lit.frag never writes depth (keeps hidden-surface removal)");
    check(writesDepth(spv::lit_reflect_frag, spv::lit_reflect_frag_size), "heat: only the reflection shader moves depth to the street");
}

// ---------------------------------------------------------------- frame pacing

void pacing() {
    using quality::FramePacer;
    auto run = [](FramePacer& p, double busyMs, double seconds, double fps) {
        int changes = 0;
        for (int i = 0; i < int(seconds * fps); ++i) changes += p.frame(busyMs, 1.0 / fps);
        return changes;
    };
    FramePacer fast;
    check(!fast.decided(), "pacing: undecided at launch (the splash waits)");
    run(fast, 9.0, 1.7, 60);
    check(fast.decided() && fast.fps() == 60 && !fast.lite(), "probe: a GPU that keeps up is cleared within the splash (~1.6 s)");
    run(fast, 9.0, 20, 60);
    check(fast.fps() == 60 && !fast.lite(), "pacing: and stays at 60 with full effects");
    FramePacer slow;
    check(run(slow, 23.0, 1.7, 60) == 1 && slow.decided(), "probe: a low-end GPU is found while the splash still covers the screen");
    check(slow.fps() == 30 && slow.lite(), "probe: lite effects and an even 30 fps, together, before anything is seen");
    check(run(slow, 3.0, 60, 30) == 0 && slow.fps() == 30 && slow.lite(), "pacing: it stays for the session (no flip-flopping)");
    FramePacer loading;
    run(loading, 60.0, 0.35, 60); // the first frames: loading, shader setup
    run(loading, 9.0, 1.4, 60);
    check(loading.decided() && !loading.lite(), "probe: slow first frames (loading) don't count");
    FramePacer later;
    run(later, 9.0, 1.7, 60);
    run(later, 23.0, 2.1, 60);
    check(!later.lite(), "in play: one slow 2 s window isn't enough (a hitch)");
    check(run(later, 23.0, 2.1, 60) == 1 && later.lite() && later.fps() == 30, "in play: too slow for 4 s -> low-end then");
    FramePacer edge;
    run(edge, 17.5, 20, 60);
    check(edge.fps() == 60 && !edge.lite(), "pacing: just under the limit keeps everything");
    FramePacer known;
    known.remembered(true);
    check(known.decided() && known.lite() && known.fps() == 30, "remembered low-end: lite at 30 from the first frame, no probe needed");

    // the game side: lite turns the street reflection off and flags the frame for the shader
    FakeStorage st = FakeStorage::ready();
    FakeClock clock;
    FakeAds ads;
    GameServices sv;
    sv.storage = &st; sv.clock = &clock; sv.ads = &ads;
    Game g(sv);
    step(g, 0.3f);
    size_t refl = 0;
    for (const auto& v : g.renderList().buckets[size_t(Pass::Reflect)]) refl += v.size();
    check(refl > 0 && !g.renderList().frame.lite, "lite off: the hen is reflected, full effects");
    g.setLite(true);
    step(g, 0.1f);
    refl = 0;
    for (const auto& v : g.renderList().buckets[size_t(Pass::Reflect)]) refl += v.size();
    check(refl == 0 && g.renderList().frame.lite, "lite on: no reflection, the shader is told to skip the animation");
    {   // the result is remembered: the next launch on this device starts lite, without a visible switch
        Game again(sv);
        check(again.lite() && again.renderList().frame.lite == false, "remembered: the next launch starts lite (before its first frame)");
        step(again, 0.1f);
        check(again.renderList().frame.lite, "...and draws lite from its first frame");
    }
    auto streaks = [&] { return g.renderList().buckets[size_t(Pass::UnlitAlpha)][size_t(MeshId::Plane)].size(); };
    const size_t liteStreaks = streaks();
    g.setLite(false);
    step(g, 0.1f);
    check(streaks() >= liteStreaks + 800, "lite on: no rain (900 streaks come back with full effects)");
    // wind streaks (speed lines) show once the run is fast: compare lite and full at speed
    auto windStreaks = [&] { return g.renderList().buckets[size_t(Pass::UnlitAdd)][size_t(MeshId::Plane)].size(); };
    g.press();
    g.debugStartSurge();
    step(g, 3.f);
    const size_t fullWind = windStreaks();
    g.setLite(true);
    step(g, 0.2f);
    check(fullWind >= windStreaks() + 40, "lite on: no wind streaks (42 at speed with full effects)");
}

// ---------------------------------------------------------------- frame timing on the refresh grid

void vsyncPacing() {
    using quality::nextFrameVsync;
    const double p = 1e9 / 60.0; // a 60 Hz panel
    const int64_t v0 = 1'000'000'000;
    auto grid = [&](int64_t t) { const double n = double(t - v0) / p; return std::abs(n - std::round(n)) < 1e-3; };
    // 30 fps on 60 Hz: every frame two refreshes after the last, whatever our own timing does
    int64_t frame = 0, vs = v0;
    bool even = true, onGrid = true;
    int64_t now = v0 + 3'000'000;
    for (int i = 0; i < 300; ++i) {
        const int64_t next = nextFrameVsync(frame, vs, p, 30, now);
        if (frame > 0) even = even && std::llabs(next - frame - int64_t(2 * p)) <= 2;
        onGrid = onGrid && grid(next);
        frame = next;
        vs = frame + int64_t(p) * (i % 2);                          // the latest refresh seen: sometimes one later
        now = frame + 1'000'000 + (i % 7) * 900'000;                // our wake-ups jitter by up to ~6 ms
    }
    check(even, "vsync: at 30 fps every frame starts exactly two refreshes after the last (no 1 / 3 refresh frames)");
    check(onGrid, "vsync: every frame starts on a refresh (phase-locked: no drift)");
    // 60 fps: every refresh
    check(nextFrameVsync(v0, v0, p, 60, v0 + 2'000'000) == v0 + int64_t(p), "vsync: at 60 fps, the next refresh");
    // a 120 Hz panel at 30 fps: every 4th refresh
    const double p120 = 1e9 / 120.0;
    check(std::llabs(nextFrameVsync(v0, v0, p120, 30, v0 + 1'000'000) - v0 - int64_t(4 * p120)) <= 2, "vsync: 120 Hz panel, 30 fps -> every 4th refresh");
    // fell behind (a long stall): start on the coming refresh, don't chase the missed ones
    const int64_t late = v0 + int64_t(10 * p) + 3'000'000;
    const int64_t r = nextFrameVsync(v0, v0 + int64_t(10 * p), p, 30, late);
    check(r >= late - int64_t(p) && r <= late + int64_t(p) && grid(r), "vsync: after a stall, the next refresh on the grid (no catch-up burst)");
    // the first frame: right away, on the grid
    const int64_t f = nextFrameVsync(0, v0, p, 30, v0 + 5'000'000);
    check(grid(f) && f >= v0 && f <= v0 + int64_t(p) + 1, "vsync: the first frame starts on the next refresh");
}

// ---------------------------------------------------------------- the Android manifest

void androidManifest() {
    std::ifstream in(CS_SOURCE_DIR "/platforms/android/app/src/main/AndroidManifest.xml");
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string m = ss.str();
    check(!m.empty(), "manifest: readable");
    const size_t at = m.find("android:configChanges=\"");
    check(at != std::string::npos, "manifest: the game activity declares configChanges");
    const std::string list = at == std::string::npos ? "" : m.substr(at, m.find('"', at + 23) - at);
    bool all = true;
    for (const char* c : {"orientation", "screenSize", "screenLayout", "smallestScreenSize", "keyboard", "keyboardHidden", "navigation",
                          "density", "uiMode", "mcc", "mnc", "locale", "layoutDirection", "fontScale", "fontWeightAdjustment", "touchscreen",
                          "colorMode", "grammaticalGender"}) {
        const std::string tok = std::string("|") + c + "|";
        const bool has = ("|" + list.substr(23) + "|").find(tok) != std::string::npos || list.find("\"" + std::string(c) + "|") != std::string::npos;
        if (!has) { std::printf("      missing: %s\n", c); all = false; }
    }
    check(all, "manifest: every configuration change handled in place (a SIM regaining signal, a language change: no stutter / restart)");
    check(m.find("android:isGame=\"true\"") != std::string::npos && m.find("android:appCategory=\"game\"") != std::string::npos,
          "manifest: declared a game (game dashboards, game mode)");
}

// ---------------------------------------------------------------- frame summary, no glow pad

void frameSummaryAndPad() {
    FakeStorage st = FakeStorage::ready();
    FakeClock clock;
    FakeAds ads;
    ads.state = RewardedState::Unavailable;
    GameServices sv;
    sv.storage = &st; sv.clock = &clock; sv.ads = &ads;
    Game g(sv);
    step(g, 0.3f);
    const std::string title = g.frameSummary();
    check(title.rfind("title", 0) == 0 && title.find("cubes ") != std::string::npos && title.find("sparks ") != std::string::npos,
          "summary: the state and what's drawn (title screen)");
    // no glow pad: no ground-flat radial glow under the hen
    auto pads = [&] {
        int n = 0;
        for (const Instance& in : g.renderList().buckets[size_t(Pass::UnlitAdd)][size_t(MeshId::Plane)]) {
            const glm::vec4 normal = in.model * glm::vec4(0, 0, 1, 0), pos = in.model * glm::vec4(0, 0, 0, 1);
            const bool flat = std::abs(normal.y) > 0.9f * glm::length(glm::vec3(normal));
            n += int(in.params.z + 0.5f) == int(Shape::RadialGlow) && flat && pos.y < 1.5f;
        }
        return n;
    };
    check(pads() == 0, "no glow pad under the hen (title)");
    g.press();
    g.debugStartSurge();
    step(g, 1.f);
    const std::string play = g.frameSummary();
    check(play.rfind("play SURGE", 0) == 0, "summary: a run, with its surge flagged");
    check(pads() == 0, "no glow pad under the hen (in a run)");
    g.setLite(true);
    step(g, 0.1f);
    check(g.frameSummary().find(" lite") != std::string::npos, "summary: lite mode flagged");
}

} // namespace

int main() {
    cyberBake();
    mirroring();
    wallMaterials();
    depthGuard();
    pacing();
    vsyncPacing();
    androidManifest();
    frameSummaryAndPad();
    return finish("render");
}
