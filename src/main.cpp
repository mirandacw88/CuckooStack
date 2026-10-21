// Desktop / testing entry point (GLFW). Runs the exact same Game + Renderer as the mobile builds,
// on any desktop Vulkan driver or on macOS through MoltenVK (Vulkan SDK).
//   ./cuckoo_stack [--validate] [--capture out.ppm [frames]] [--autoplay]
//   --capture writes the swapchain image after N frames (default 120) and exits: golden-image / CI checks.
//   --autoplay taps automatically so captures show a run in progress; --tap-once starts a run and lets it end
//   --level N plays campaign level N; --saw D sets a saw D m ahead once the run starts (--cube: on the next wall top);
//   --raid F N has the droid raid from frame F on (N shots: 1 or 2), as soon as it's allowed;
//   --bot lets the developer menu's bot play; --gauntlet starts every run at its level's maze gauntlet;
//   --gauntlet-file PATH plays the gauntlet in PATH (rows top first, ';' lines notes; named for its file, e.g.
//   assets/gauntlets/wells.txt) instead of the built-in one of that name (the level editor's Play); --level-file PATH
//   plays the level in PATH (LevelData.h; numbered for its file, e.g. assets/levels/level_07.txt) instead of the
//   built-in one; with --level N, --start-at M starts every run M metres along it (the meter full, no eggs) and
//   --start-cell COL ROW [left] at that cell of its maze gauntlet, running right (or left): the level editor's
//   play-test, where a cleared level is played again (as with --gauntlet) rather than the next;
//   --capture-range FROM TO STEP PREFIX writes frames FROM..TO every STEP; --window W H sizes the window (points)
//   (game-over screen). Captures run on a throwaway in-memory save (a 13+ player who has claimed today's drop);
//   --fresh starts from an empty one instead (age screen). --screen NAME opens a meta-game screen (shop, locker,
//   missions, drop, settings, levelup, streak, gate, notif, replay, starter, continue, age), --coins N adds coins and
//   --tap LABEL presses the button with that label at frame 30, --rc key=value fakes a Remote Config value.
// Controls: space / enter / up = lay egg, click = tap, M = mute, Esc = quit.
#include "core/FileStorage.h"
#include "core/Gauntlet.h"
#include "core/Game.h"
#include "core/LevelData.h"
#include "core/audio/Synth.h"
#include "graphics/Renderer.h"
#include "platform/PlatformSurface.h"
#include "platform/desktop/DesktopAudio.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>

// GLFW's Vulkan helpers are declared only when vulkan.h is visible; declare the two we use against volk's types.
extern "C" {
VkResult glfwCreateWindowSurface(VkInstance, GLFWwindow*, const VkAllocationCallbacks*, VkSurfaceKHR*);
void glfwInitVulkanLoader(PFN_vkGetInstanceProcAddr);
}

namespace {

class GlfwSurface final : public cs::PlatformSurface {
public:
    explicit GlfwSurface(GLFWwindow* w) : window_(w) {}
    VkResult loadVulkan() override { return volkGetInstanceVersion() ? VK_SUCCESS : volkInitialize(); }
    std::vector<const char*> requiredInstanceExtensions() const override {
        uint32_t n = 0;
        const char** names = glfwGetRequiredInstanceExtensions(&n);
        return std::vector<const char*>(names, names + n);
    }
    VkResult createSurface(VkInstance instance, VkSurfaceKHR* out) override { return glfwCreateWindowSurface(instance, window_, nullptr, out); }
    VkExtent2D drawableExtent() const override {
        int w = 0, h = 0;
        glfwGetFramebufferSize(window_, &w, &h);
        return {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    }
    float contentScale() const override { float x = 1, y = 1; glfwGetWindowContentScale(window_, &x, &y); return x; }
    const char* name() const override { return "GLFW"; }

private:
    GLFWwindow* window_;
};

// throwaway save for captures and screenshots, so they never touch the player's own save file
struct MemoryStorage final : cs::IStorage {
    std::map<std::string, std::string> kv;
    std::optional<std::string> get(const std::string& k) override { auto it = kv.find(k); if (it == kv.end()) return std::nullopt; return it->second; }
    void set(const std::string& k, const std::string& v) override { kv[k] = v; }
};

// --rc key=value: pretend Remote Config sent this value (live events, A/B variants) for screenshots
struct FlagRemoteConfig final : cs::IRemoteConfig {
    std::map<std::string, double> values;
    std::optional<double> number(const std::string& k) override { auto it = values.find(k); if (it == values.end()) return std::nullopt; return it->second; }
};

struct App {
    cs::Game* game = nullptr;
    cs::Renderer* renderer = nullptr;
};

} // namespace

int main(int argc, char** argv) {
    bool validate = false;
#ifndef NDEBUG
    validate = true;
#endif
    std::string capturePath;
    int captureFrame = 120;
    bool autoplay = false, tapOnce = false, forceSurge = false, gpuTiming = false, fresh = false;
    std::string screenName, tapLabel, endCardPath, outfit;
    FlagRemoteConfig flagRc;
    int extraCoins = 0, startLevel = 0, rangeFrom = -1, rangeTo = -1, rangeStep = 1;
    std::string rangePrefix;
    double sawAhead = -1;
    bool cubeSaw = false, botPlays = false, nearGauntlet = false;
    double startAt = -1;
    int startCol = -1, startRow = -1, startDir = 1;
    int raidAt = -1, raidShots = 1;
    float renderScale = 1.f;
    int winW = 430, winH = 860;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--validate") == 0) validate = true;
        else if (std::strcmp(argv[i], "--autoplay") == 0) autoplay = true;
        else if (std::strcmp(argv[i], "--tap-once") == 0) tapOnce = true;
        else if (std::strcmp(argv[i], "--surge") == 0) forceSurge = true;
        else if (std::strcmp(argv[i], "--gpu-timing") == 0) gpuTiming = true;
        else if (std::strcmp(argv[i], "--fresh") == 0) fresh = true;
        else if (std::strcmp(argv[i], "--screen") == 0 && i + 1 < argc) screenName = argv[++i];
        else if (std::strcmp(argv[i], "--coins") == 0 && i + 1 < argc) extraCoins = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--tap") == 0 && i + 1 < argc) tapLabel = argv[++i]; // taps that button at frame 30
        else if (std::strcmp(argv[i], "--outfit") == 0 && i + 1 < argc) outfit = argv[++i]; // wear a cosmetic, e.g. hen_lava
        else if (std::strcmp(argv[i], "--endcard") == 0 && i + 1 < argc) endCardPath = argv[++i]; // writes the replay end card
        else if (std::strcmp(argv[i], "--rc") == 0 && i + 1 < argc) {
            const std::string kv = argv[++i];
            const size_t eq = kv.find('=');
            if (eq != std::string::npos) flagRc.values[kv.substr(0, eq)] = std::atof(kv.c_str() + eq + 1);
        }
        else if (std::strcmp(argv[i], "--render-scale") == 0 && i + 1 < argc) renderScale = float(std::atof(argv[++i])); // start a surge right after the run begins
        else if (std::strcmp(argv[i], "--level") == 0 && i + 1 < argc) startLevel = std::atoi(argv[++i]); // play this campaign level
        else if (std::strcmp(argv[i], "--window") == 0 && i + 2 < argc) { winW = std::atoi(argv[++i]); winH = std::atoi(argv[++i]); }
        else if (std::strcmp(argv[i], "--saw") == 0 && i + 1 < argc) sawAhead = std::atof(argv[++i]); // a saw this far ahead once the run starts
        else if (std::strcmp(argv[i], "--cube") == 0) cubeSaw = true; // ...on the next wall top instead (a cube saw)
        else if (std::strcmp(argv[i], "--raid") == 0 && i + 2 < argc) { raidAt = std::atoi(argv[++i]); raidShots = std::atoi(argv[++i]); } // the droid raids at that frame
        else if (std::strcmp(argv[i], "--bot") == 0) botPlays = true;  // the developer menu's bot plays
        else if (std::strcmp(argv[i], "--gauntlet") == 0) nearGauntlet = true; // every run starts just before its level's maze gauntlet
        else if (std::strcmp(argv[i], "--start-at") == 0 && i + 1 < argc) startAt = std::atof(argv[++i]); // ...or this far along (metres)
        else if (std::strcmp(argv[i], "--start-cell") == 0 && i + 2 < argc) { // ...or at this cell of its maze ([left]: running left)
            startCol = std::atoi(argv[++i]); startRow = std::atoi(argv[++i]);
            if (i + 1 < argc && std::strcmp(argv[i + 1], "left") == 0) { startDir = -1; ++i; }
        }
        else if (std::strcmp(argv[i], "--gauntlet-file") == 0 && i + 1 < argc) { // play this layout for the gauntlet it's named for
            const std::string path = argv[++i];
            std::ifstream in(path);
            std::vector<std::string> rows;
            for (std::string line; std::getline(in, line);) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty() && line[0] != ';') rows.push_back(line);
            }
            std::string name = path.substr(path.find_last_of("/\\") + 1), error;
            name = name.substr(0, name.find('.'));
            if (!in.is_open() || !cs::debugOverrideGauntlet(name, rows, &error)) {
                std::fprintf(stderr, "--gauntlet-file %s: %s\n", path.c_str(), in.is_open() ? error.c_str() : "can't read it");
                return 1;
            }
        }
        else if (std::strcmp(argv[i], "--level-file") == 0 && i + 1 < argc) { // play this level for the one it's numbered for
            const std::string path = argv[++i];
            std::ifstream in(path);
            std::string text, error;
            for (std::string line; std::getline(in, line);) text += line + "\n";
            const std::string file = path.substr(path.find_last_of("/\\") + 1);
            const int number = std::atoi(file.c_str() + std::min(file.size(), file.find_first_of("0123456789")));
            if (!in.eof() || number <= 0 || !cs::debugOverrideLevel(number, text, &error)) {
                std::fprintf(stderr, "--level-file %s: %s\n", path.c_str(), number <= 0 ? "named for no level (level_07.txt)" : error.empty() ? "can't read it" : error.c_str());
                return 1;
            }
        }
        else if (std::strcmp(argv[i], "--capture-range") == 0 && i + 4 < argc) { // frames from..to every step, to prefix_NNNN.ppm
            rangeFrom = std::atoi(argv[++i]); rangeTo = std::atoi(argv[++i]); rangeStep = std::max(1, std::atoi(argv[++i])); rangePrefix = argv[++i];
        }
        else if (std::strcmp(argv[i], "--capture") == 0 && i + 1 < argc) {
            capturePath = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') captureFrame = std::atoi(argv[++i]);
        }
    }

    // Share one loader between volk and GLFW (must happen before glfwInit).
    if (volkInitialize() != VK_SUCCESS) {
        std::fprintf(stderr, "Cuckoo Stack OVERDRIVE needs a Vulkan driver (install the Vulkan SDK / MoltenVK on macOS).\n");
        return 1;
    }
    glfwInitVulkanLoader(vkGetInstanceProcAddr);
    if (!glfwInit() || !glfwVulkanSupported()) { std::fprintf(stderr, "GLFW could not initialise Vulkan.\n"); return 1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(winW, winH, "Cuckoo Stack OVERDRIVE", nullptr, nullptr); // phone-shaped by default
    if (!window) { std::fprintf(stderr, "Window creation failed.\n"); glfwTerminate(); return 1; }

    GlfwSurface surface(window);
    cs::Renderer renderer;
    std::string error;
    renderer.setPipelineCachePath("vk_pipeline_cache.bin");
    if (!renderer.init(surface, validate, error)) {
        std::fprintf(stderr, "Vulkan initialisation failed: %s\n", error.c_str());
        renderer.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    renderer.setGpuTiming(gpuTiming);
    renderer.setRenderScale(renderScale);
    cs::FileStorage fileStorage("cuckoo-stack.save");
    MemoryStorage memStorage;
    const bool ranged = rangeFrom >= 0;
    const bool scripted = !capturePath.empty() || ranged || autoplay || tapOnce || forceSurge || !screenName.empty() || fresh || startLevel > 0;
    if (startLevel > 0) memStorage.kv["cluckstack-campaign"] = "level=" + std::to_string(startLevel) + ";";
    if (scripted && !fresh) {
        memStorage.kv["cluckstack-profile"] = "aud=2;sessions=1;";
        char today[16];
        const std::time_t now = std::time(nullptr);
        std::strftime(today, sizeof today, "%Y-%m-%d", std::localtime(&now));
        memStorage.kv["cluckstack-drop"] = std::string("claims=1;last=") + today + ";";
    }
    cs::IStorage& storage = scripted ? static_cast<cs::IStorage&>(memStorage) : fileStorage;
    cs::NullHaptics haptics;
    cs::audio::Synth synth;
    cs::GameServices services;
    services.audio = &synth; services.storage = &storage; services.haptics = &haptics; services.remoteConfig = &flagRc;
    cs::Game game(services);
    if (extraCoins > 0) game.debugAddCoins(extraCoins);
    if (!outfit.empty()) game.debugWear(outfit);
    if (botPlays) game.debugSetBot({true, false, false, 0});
    if (nearGauntlet) game.debugNearGauntlet(true);
    if (startLevel > 0 && (nearGauntlet || startAt > 0 || startCol >= 0)) // the level editor's play-test: every run on that level
        game.debugPlayTest({startLevel, startAt, startCol, startRow, startDir});
    if (!endCardPath.empty()) { // the Android replay end card, for review: --endcard out.ppm
        cs::ReplayMeta m;
        m.distance = 412; m.newBest = true; m.label = "LEVEL 3";
        const std::vector<uint8_t> px = game.endCard(720, 1600, m);
        if (FILE* f = std::fopen(endCardPath.c_str(), "wb")) {
            std::fprintf(f, "P6\n720 1600\n255\n");
            for (size_t i = 0; i < px.size(); i += 4) std::fwrite(&px[i], 1, 3, f);
            std::fclose(f);
        }
        return 0;
    }
    cs::DesktopAudio audioOut;
    if (capturePath.empty() && !ranged) audioOut.start(synth); // captures stay silent and deterministic
    App app{&game, &renderer};
    glfwSetWindowUserPointer(window, &app);

    int ww = 0, wh = 0;
    glfwGetWindowSize(window, &ww, &wh);
    game.resize(float(ww), float(wh));

    glfwSetWindowSizeCallback(window, [](GLFWwindow* w, int width, int height) {
        auto* a = static_cast<App*>(glfwGetWindowUserPointer(w));
        a->game->resize(float(width), float(height));
        a->renderer->requestResize();
    });
    glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int, int action, int) {
        auto* a = static_cast<App*>(glfwGetWindowUserPointer(w));
        if (action != GLFW_PRESS) return; // ignore auto-repeat, like the web build
        if (key == GLFW_KEY_SPACE || key == GLFW_KEY_ENTER || key == GLFW_KEY_UP) a->game->press();
        else if (key == GLFW_KEY_M) a->game->toggleMute();
        else if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w, GLFW_TRUE);
    });
    glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int) {
        if (button != GLFW_MOUSE_BUTTON_LEFT) return;
        auto* a = static_cast<App*>(glfwGetWindowUserPointer(w));
        double x = 0, y = 0;
        glfwGetCursorPos(w, &x, &y); // window points, same units as the HUD
        if (action == GLFW_PRESS) a->game->touch(cs::TouchPhase::Began, float(x), float(y));
        else if (action == GLFW_RELEASE) a->game->touch(cs::TouchPhase::Ended, float(x), float(y));
    });
    glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) { // a drag on a scrolling list
        if (glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS) return;
        static_cast<App*>(glfwGetWindowUserPointer(w))->game->touch(cs::TouchPhase::Moved, float(x), float(y));
    });
    glfwSetScrollCallback(window, [](GLFWwindow* w, double, double dy) {
        static_cast<App*>(glfwGetWindowUserPointer(w))->game->scrollBy(cs::wheelToScroll(dy));
    });

    auto prev = std::chrono::steady_clock::now();
    auto fpsStart = std::chrono::steady_clock::now();
    int fpsFrames = 0;
    double cpuUpdate = 0, cpuRender = 0;
    for (int frame = 0; !glfwWindowShouldClose(window); ++frame) {
        glfwPollEvents();
        const auto now = std::chrono::steady_clock::now();
        // captures use a fixed 60 Hz step so the image is reproducible
        const double dt = capturePath.empty() && !ranged ? std::chrono::duration<double>(now - prev).count() : 1.0 / 60.0;
        prev = now;
        if (autoplay && frame % 20 == 10) game.press(); // starts the run, then lays an egg every 1/3 s
        if (tapOnce && frame == 10) game.press();
        if (sawAhead >= 0 && frame == 12) game.debugSaw(sawAhead, cubeSaw);
        if (raidAt >= 0 && frame >= raidAt && !game.droidRaiding() && game.droidShotsFired() == 0) game.debugDroidRaid(raidShots);
        if (forceSurge && frame == 40) game.debugStartSurge();
        if (!screenName.empty() && frame == 2) game.debugOpenScreen(screenName);
        if (!tapLabel.empty() && frame == 30) game.tapButton(tapLabel);
        const auto u0 = std::chrono::steady_clock::now();
        game.update(dt);
        const auto u1 = std::chrono::steady_clock::now();
        cpuUpdate += std::chrono::duration<double, std::milli>(u1 - u0).count();
        if (!capturePath.empty() && frame == captureFrame) renderer.captureNextFrame(capturePath);
        if (ranged && frame >= rangeFrom && frame <= rangeTo && (frame - rangeFrom) % rangeStep == 0) {
            char path[1024];
            std::snprintf(path, sizeof path, "%s_%05d.ppm", rangePrefix.c_str(), frame);
            renderer.captureNextFrame(path);
        }
        if (!renderer.render(game.renderList())) { std::fprintf(stderr, "Device lost.\n"); break; }
        cpuRender += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - u1).count();
        if (gpuTiming && ++fpsFrames == 240) {
            const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - fpsStart).count();
            std::printf("[CuckooStack] %.0f fps (%.2f ms/frame); CPU game.update %.2f ms, renderer.render %.2f ms (incl. waits)\n", fpsFrames / s,
                        1000.0 * s / fpsFrames, cpuUpdate / fpsFrames, cpuRender / fpsFrames);
            cpuUpdate = cpuRender = 0;
            std::fflush(stdout);
            fpsFrames = 0; fpsStart = std::chrono::steady_clock::now();
        }
        if (!capturePath.empty() && frame > captureFrame) break;
        if (ranged && frame > rangeTo) break;
    }
    renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
