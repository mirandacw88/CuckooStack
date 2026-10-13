// The Android manifest (platforms/android/app/src/main/AndroidManifest.xml): every configuration change handled in
// place (no window rework / restart mid-run), and declared a game.
#include "TestUtil.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace cs;
using namespace test;

namespace {

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

} // namespace

int main() {
    androidManifest();
    return finish("AndroidManifest");
}
