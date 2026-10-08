// Desktop / testing entry point (GLFW). Runs the exact same Game + Renderer as the mobile builds,
// on any desktop Vulkan driver or on macOS through MoltenVK (Vulkan SDK).
//   ./cuckoo_stack [--validate] [--capture out.ppm [frames]] [--autoplay]
//   --capture writes the swapchain image after N frames (default 120) and exits: golden-image / CI checks.
//   --autoplay taps automatically so captures show a run in progress; --tap-once starts a run and lets it end
//   (game-over screen).
// Controls: space / enter / up = lay egg, click = tap, M = mute, Esc = quit.
#include "core/FileStorage.h"
#include "core/Game.h"
#include "core/audio/Synth.h"
#include "graphics/Renderer.h"
#include "platform/PlatformSurface.h"
#include "platform/desktop/DesktopAudio.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    bool autoplay = false, tapOnce = false, forceSurge = false, gpuTiming = false;
    float renderScale = 1.f;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--validate") == 0) validate = true;
        else if (std::strcmp(argv[i], "--autoplay") == 0) autoplay = true;
        else if (std::strcmp(argv[i], "--tap-once") == 0) tapOnce = true;
        else if (std::strcmp(argv[i], "--surge") == 0) forceSurge = true;
        else if (std::strcmp(argv[i], "--gpu-timing") == 0) gpuTiming = true;
        else if (std::strcmp(argv[i], "--render-scale") == 0 && i + 1 < argc) renderScale = float(std::atof(argv[++i])); // start a surge right after the run begins
        else if (std::strcmp(argv[i], "--capture") == 0 && i + 1 < argc) {
            capturePath = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') captureFrame = std::atoi(argv[++i]);
        }
    }

    // Share one loader between volk and GLFW (must happen before glfwInit).
    if (volkInitialize() != VK_SUCCESS) {
        std::fprintf(stderr, "Cuckoo Stack needs a Vulkan driver (install the Vulkan SDK / MoltenVK on macOS).\n");
        return 1;
    }
    glfwInitVulkanLoader(vkGetInstanceProcAddr);
    if (!glfwInit() || !glfwVulkanSupported()) { std::fprintf(stderr, "GLFW could not initialise Vulkan.\n"); return 1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(430, 860, "Cuckoo Stack", nullptr, nullptr); // phone-shaped by default
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
    cs::FileStorage storage("cuckoo-stack.save");
    cs::NullHaptics haptics;
    cs::audio::Synth synth;
    cs::Game game({&synth, &storage, &haptics});
    cs::DesktopAudio audioOut;
    if (capturePath.empty()) audioOut.start(synth); // captures stay silent and deterministic
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
        if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS) return;
        auto* a = static_cast<App*>(glfwGetWindowUserPointer(w));
        double x = 0, y = 0;
        glfwGetCursorPos(w, &x, &y); // window points, same units as the HUD
        a->game->pressAt(float(x), float(y));
    });

    auto prev = std::chrono::steady_clock::now();
    auto fpsStart = std::chrono::steady_clock::now();
    int fpsFrames = 0;
    double cpuUpdate = 0, cpuRender = 0;
    for (int frame = 0; !glfwWindowShouldClose(window); ++frame) {
        glfwPollEvents();
        const auto now = std::chrono::steady_clock::now();
        // captures use a fixed 60 Hz step so the image is reproducible
        const double dt = capturePath.empty() ? std::chrono::duration<double>(now - prev).count() : 1.0 / 60.0;
        prev = now;
        if (autoplay && frame % 20 == 10) game.press(); // starts the run, then lays an egg every 1/3 s
        if (tapOnce && frame == 10) game.press();
        if (forceSurge && frame == 40) game.debugStartSurge();
        const auto u0 = std::chrono::steady_clock::now();
        game.update(dt);
        const auto u1 = std::chrono::steady_clock::now();
        cpuUpdate += std::chrono::duration<double, std::milli>(u1 - u0).count();
        if (!capturePath.empty() && frame == captureFrame) renderer.captureNextFrame(capturePath);
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
    }
    renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
