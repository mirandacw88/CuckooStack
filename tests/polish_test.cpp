// The polish round, checked headless through the HUD's drawn strings (Game::debugRecordHud):
//  - the OVERDRIVE line under the title logo (HudStyle.h overdriveLook) and on the Share Replay end card
//  - the rename to "Cuckoo Stack OVERDRIVE" (app names, settings footer, window, web pages, the iOS card)
//  - "surge" shown to the player as "overdrive" (boost tile, timer, popup, mission), the boost names fitting their
//    tiles (fitWidth), and the "Level Complete" banner's look (its placement and timing: src/core/Campaign_test.cpp)
#include "TestUtil.h"
#include "core/Daily.h"
#include "core/HudStyle.h"

#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

using namespace cs;
using namespace cs::hud;
using namespace test;

namespace {

struct Env {
    FakeStorage st = FakeStorage::ready();
    FakeClock clock;
    FakeAds ads;
    GameServices svc() { GameServices s; s.storage = &st; s.clock = &clock; s.ads = &ads; return s; }
};

std::string lower(std::string s) { for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; }
bool same(glm::vec3 a, glm::vec3 b) { return glm::length(a - b) < 1e-4f; }
std::string file(const char* rel) {
    std::ifstream f(std::string(CS_SOURCE_DIR) + "/" + rel);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}
bool has(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }

std::vector<const Game::HudString*> find(const Game& g, const std::string& text) {
    std::vector<const Game::HudString*> out;
    for (const auto& h : g.hudStrings()) if (h.text == text) out.push_back(&h);
    return out;
}
bool anySurge(const Game& g) {
    for (const auto& h : g.hudStrings()) if (has(lower(h.text), "surge")) return true;
    return false;
}

// ---------------------------------------------------------------- the OVERDRIVE line

void overdriveStyle() {
    const OverdriveLook l = overdriveLook(72.f);
    check(std::abs(l.over.size - 36.f) < 1e-4f && l.over.size == l.under.size, "overdrive: half the logo's size");
    check(same(l.over.color, kInk), "overdrive: dark letters");
    int ring = 0;
    glm::vec2 sum{0.f};
    float r = 0;
    for (const TextShadow& s : l.over.shadows) {
        if (s.blur == 0.f && same(s.color, kNeon) && s.alpha == 1.f) { ring++; sum += s.offset; r = std::max(r, glm::length(s.offset)); }
    }
    check(ring == int(l.over.shadows.size()) && ring >= 12, "overdrive: outlined all round in the blocks' neon blue (#29e7ff)");
    check(glm::length(sum) < 1e-3f && r > 1.f, "overdrive: an even outline (no side heavier)");
    // under: the logo's effect, shifted out past the outline on both sides
    check(same(l.under.color, kHot) && l.underDx < -r, "overdrive: a pink copy peeks out on the left");
    bool blueRight = false, pinkGlow = false;
    for (const TextShadow& s : l.under.shadows) {
        if (s.blur == 0.f && same(s.color, kNeon) && l.underDx + s.offset.x > r) blueRight = true;
        if (s.blur > 0.f && same(s.color, kHot)) pinkGlow = true;
    }
    check(blueRight, "overdrive: a blue copy peeks out on the right");
    check(pinkGlow, "overdrive: the logo's pink glow (in the under pass, so it can't tint the outline)");
    bool overGlow = false;
    for (const TextShadow& s : l.over.shadows) overGlow = overGlow || s.blur > 0.f;
    check(!overGlow, "overdrive: no glow in the outlined pass");
}

void titleLogo() {
    Env env;
    Game g(env.svc());
    g.resize(430, 900);
    g.debugRecordHud(true);
    step(g, 2.f); // past the glitch-in, before the 5 s glitch
    const auto od = find(g, "OVERDRIVE");
    check(od.size() == 2, "title: OVERDRIVE drawn (the split / glow pass and the outlined pass)");
    const Game::HudString* logo = nullptr;
    for (const auto& h : g.hudStrings())
        if (h.style.font == FontId::Display && (h.text == "Cuckoo Stack" || h.text == "Stack")) logo = &h;
    check(logo != nullptr, "title: the Cuckoo Stack logo is there");
    if (od.size() == 2 && logo) {
        const Game::HudString& under = *od[0];
        const Game::HudString& over = *od[1];
        check(over.top > logo->top + 0.5f * logo->style.size, "title: OVERDRIVE sits under Cuckoo Stack");
        check(std::abs(over.x - 215.f) < 0.5f && std::abs(under.x - (215.f + overdriveLook(logo->style.size).underDx)) < 0.5f,
              "title: centred, the split layer drawn first and offset");
        check(std::abs(over.style.size - logo->style.size * 0.5f) < 0.01f, "title: half the logo's size");
        check(same(over.style.color, kInk) && same(under.style.color, kHot), "title: outlined letters over the pink layer");
        check(over.width < 430 - 32, "title: fits across the screen");
        check(over.style.opacity == logo->style.opacity, "title: fades with the logo");
    }
    // it glitches with the logo: same sideways jolt
    bool together = true, jolted = false;
    for (int i = 0; i < 60 * 6; ++i) {
        g.update(1.0 / 60.0);
        const auto o = find(g, "OVERDRIVE");
        const Game::HudString* l = nullptr;
        for (const auto& h : g.hudStrings()) if (h.style.font == FontId::Display && h.text == "Stack") l = &h;
        if (o.size() != 2 || !l) continue;
        const float logoDx = l->x - 215.f, odDx = o[1]->x - 215.f;
        together = together && std::abs(logoDx - odDx) < 0.01f;
        jolted = jolted || std::abs(odDx) > 1.f;
    }
    check(together && jolted, "title: OVERDRIVE glitches with the logo");
}

void endCardOverdrive() {
    Env env;
    Game g(env.svc());
    ReplayMeta m;
    m.distance = 215; m.label = "LEVEL 3";
    const int W = 720, H = 1280;
    const std::vector<uint8_t> px = g.endCard(W, H, m);
    // the OVERDRIVE line: between the name (mid + 340) and the store line (mid + 500), at 720 px wide S = 1
    const int mid = int(H * 0.42f), y0 = mid + 422, y1 = mid + 422 + 40;
    int neon = 0, ink = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = 0; x < W; ++x) {
            const uint8_t* p = &px[(size_t(y) * W + x) * 4];
            if (p[0] < 120 && p[1] > 170 && p[2] > 200) neon++;      // the blue outline
            if (p[0] < 20 && p[1] < 20 && p[2] < 30) ink++;          // dark letters / background
        }
    check(neon > 400, "end card: OVERDRIVE's blue outline under the name");
    check(ink > 0, "end card: with dark letters inside");
}

// ---------------------------------------------------------------- the rename

void rename() {
    const std::string cmake = file("CMakeLists.txt"), gradle = file("platforms/android/app/build.gradle.kts");
    check(has(cmake, "set(CS_APP_NAME \"Cuckoo Stack OVERDRIVE\")") && has(cmake, "set(CS_APP_NAME \"Cuckoo Stack OVERDRIVE β\")"),
          "rename: iOS app name (prod and staging)");
    check(has(gradle, "\"app_name\", \"Cuckoo Stack OVERDRIVE\")") && has(gradle, "\"app_name\", \"Cuckoo Stack OVERDRIVE \\u03b2\")"),
          "rename: Android app name (prod and staging)");
    check(has(cmake, "com.cuckoostack.aerospheregames\"") && has(gradle, "cuckoostack"), "rename: the store IDs are untouched");
    check(has(file("src/main.cpp"), "\"Cuckoo Stack OVERDRIVE\", nullptr, nullptr"), "rename: desktop window title");
    check(has(file("src/graphics/VulkanContext.h"), "appName = \"Cuckoo Stack OVERDRIVE\""), "rename: Vulkan app name");
    check(has(file("src/platform/android/AndroidMain.cpp"), "Cuckoo Stack OVERDRIVE needs Vulkan") &&
          has(file("src/platform/ios/IOSApp.mm"), "Cuckoo Stack OVERDRIVE could not start"), "rename: the start-up error messages");
    const std::string swift = file("src/platform/ios/IOSServices.swift");
    check(has(swift, "\"CUCKOO STACK OVERDRIVE\"") && has(swift, "outlined(\"OVERDRIVE\"") && has(swift, "Cuckoo Stack OVERDRIVE \\(distance)m.mp4"),
          "rename: the iOS replay (watermark, end card, file name)");
    const std::string home = file("firebase/hosting/public/index.html"), challenge = file("firebase/hosting/public/c/index.html");
    check(has(home, "<title>Cuckoo Stack OVERDRIVE</title>") && has(home, "<h1>Cuckoo Stack OVERDRIVE</h1>"), "rename: the website");
    check(has(challenge, "Beat my run on Cuckoo Stack OVERDRIVE") && !has(challenge, "Cuckoo Stack course"), "rename: the challenge link page");

    Env env;
    Game g(env.svc());
    g.resize(430, 900);
    g.debugRecordHud(true);
    step(g, 0.5f);
    check(g.tapButton("Settings"), "settings opens");
    step(g, 0.5f);
    check(!find(g, "Cuckoo Stack OVERDRIVE · staging").empty() || !find(g, "Cuckoo Stack OVERDRIVE").empty(),
          "rename: the settings footer");
}

// ---------------------------------------------------------------- surge shown as overdrive

void overdriveNames() {
    check(Missions::describe({MissionKind::Surges, 1}) == "Trigger overdrive", "mission: \"Trigger overdrive\"");
    check(Missions::describe({MissionKind::Surges, 3}) == "Trigger overdrive 3 times", "mission: \"Trigger overdrive 3 times\"");

    Env env;
    Game g(env.svc());
    g.resize(430, 900);
    g.debugRecordHud(true);
    step(g, 1.f);
    check(!find(g, "Overdrive start").empty(), "title: the boost tile reads \"Overdrive start\"");
    check(!anySurge(g), "title: no \"surge\" anywhere");
    g.press();
    step(g, 0.3f);
    g.debugStartSurge();
    g.update(1.0 / 60.0);
    bool pop = false, timer = false, surgeShown = false;
    for (int i = 0; i < 60 * 2 && g.state() == Game::State::Play; ++i) {
        g.update(1.0 / 60.0);
        pop = pop || !find(g, "OVERDRIVE!").empty();
        for (const auto& h : g.hudStrings()) timer = timer || h.text.rfind("Overdrive \u00b7 ", 0) == 0;
        surgeShown = surgeShown || anySurge(g);
    }
    check(pop, "run: \"OVERDRIVE!\" pops up when it starts");
    check(timer, "run: the timer reads \"OVERDRIVE · 5.0s\"");
    check(!surgeShown, "run: no \"surge\" anywhere");
}

void boostLabelsFit() {
    FontAtlas fonts;
    check(fonts.build(), "fonts");
    TextLayout tl(fonts);
    const TextStyle base = style(FontId::BodyBold, 12, kText, 0.12f, true);
    const TextStyle wide = fitWidth(tl, "OVERCLOCK", base, 1000.f);
    check(wide.size == base.size && wide.letterSpacing == base.letterSpacing, "fit: a name with room is left alone");
    const float full = tl.measure("OVERDRIVE START", base);
    const TextStyle tight = fitWidth(tl, "OVERDRIVE START", base, full - 4.f);
    check(tight.size == base.size && tight.letterSpacing < base.letterSpacing && tl.measure("OVERDRIVE START", tight) <= full - 4.f,
          "fit: a little over: tighter spacing, same size");
    const TextStyle small = fitWidth(tl, "OVERDRIVE START", base, full * 0.6f);
    check(small.size < base.size && tl.measure("OVERDRIVE START", small) <= full * 0.6f + 0.01f, "fit: well over: smaller too");

    // on the title screen, at phone widths from the SE up
    bool fits = true, overclock = true;
    for (float W : {320.f, 375.f, 393.f, 430.f, 440.f}) {
        Env env;
        Game g(env.svc());
        g.resize(W, 860);
        g.debugRecordHud(true);
        step(g, 1.f);
        const float cw = std::min(168.f, (W - 44) / 2);
        const auto od = find(g, "Overdrive start"), oc = find(g, "Overclock");
        fits = fits && !od.empty() && od[0]->width <= cw - 58 + 0.01f;
        overclock = overclock && !oc.empty() && oc[0]->style.size == 12.f;
    }
    check(fits, "title: \"Overdrive start\" fits its tile on every phone width");
    check(overclock, "title: \"Overclock\" keeps its size");
}

// ---------------------------------------------------------------- the finish line

void levelComplete() {
    Env env;
    Game g(env.svc());
    g.resize(430, 900);
    g.debugRecordHud(true);
    g.debugNearFinish();
    g.press();
    g.debugStartSurge();
    bool shown = false, look = true, finishPop = false;
    for (int i = 0; i < 60 * 12 && g.state() == Game::State::Play; ++i) {
        g.update(1.0 / 60.0);
        finishPop = finishPop || !find(g, "FINISH!").empty();
        for (const char* line : {"Level Complete", "Level", "Complete"})
            for (const auto* h : find(g, line)) {
                if (h->style.font != FontId::Display) continue;
                shown = true;
                bool pink = false, blue = false, glow = false;
                for (const TextShadow& s : h->style.shadows) {
                    pink = pink || (s.blur == 0.f && same(s.color, kHot) && s.offset.x < 0);
                    blue = blue || (s.blur == 0.f && same(s.color, kNeon) && s.offset.x > 0);
                    glow = glow || (s.blur > 0.f && same(s.color, kHot));
                }
                look = look && same(h->style.color, kText) && pink && blue && glow;
            }
    }
    check(shown, "finish: \"Level Complete\" is drawn");
    check(look, "finish: white, with the pink / blue split and pink glow");
    check(!finishPop, "finish: the old \"FINISH!\" popup is gone (the banner replaces it)");
}

void recordingIsOptIn() {
    Env env;
    Game g(env.svc());
    g.resize(430, 900);
    step(g, 1.f);
    check(g.hudStrings().empty(), "HUD recording: off unless a test switches it on");
}

} // namespace

int main() {
    overdriveStyle();
    titleLogo();
    endCardOverdrive();
    rename();
    overdriveNames();
    boostLabelsFit();
    levelComplete();
    recordingIsOptIn();
    return test::finish("polish");
}
