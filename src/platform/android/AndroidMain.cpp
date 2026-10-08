// Android entry point (GameActivity + native_app_glue): lifecycle, touch input, storage, haptics and the
// explicit diagnostic exit for devices whose Vulkan loader or driver cannot run the game.
#include "AndroidAudio.h"
#include "AndroidWindow.h"
#include "../../core/FileStorage.h"
#include "../../core/Game.h"
#include "../../core/audio/Synth.h"
#include "../../graphics/Quality.h"
#include "../../graphics/Renderer.h"

#include <android/log.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <sys/system_properties.h>
#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <jni.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <memory>
#include <thread>
#include <string>

namespace {

constexpr const char* kTag = "CuckooStack";

// Calls a method on CuckooActivity (platforms/android/.../CuckooActivity.java).
class JavaBridge {
public:
    explicit JavaBridge(android_app* app) : app_(app) {
        app_->activity->vm->AttachCurrentThread(&env_, nullptr);
    }
    ~JavaBridge() { app_->activity->vm->DetachCurrentThread(); }

    void callVoid(const char* name, const char* sig, ...) {
        jobject obj = app_->activity->javaGameActivity;
        jclass cls = env_->GetObjectClass(obj);
        jmethodID m = env_->GetMethodID(cls, name, sig);
        if (m) {
            va_list args;
            va_start(args, sig);
            env_->CallVoidMethodV(obj, m, args);
            va_end(args);
        }
        if (env_->ExceptionCheck()) env_->ExceptionClear();
        env_->DeleteLocalRef(cls);
    }
    bool callBool(const char* name) {
        jobject obj = app_->activity->javaGameActivity;
        jclass cls = env_->GetObjectClass(obj);
        jmethodID m = env_->GetMethodID(cls, name, "()Z");
        const bool r = m && env_->CallBooleanMethod(obj, m);
        if (env_->ExceptionCheck()) env_->ExceptionClear();
        env_->DeleteLocalRef(cls);
        return r;
    }
    int callInt(const char* name) {
        jobject obj = app_->activity->javaGameActivity;
        jclass cls = env_->GetObjectClass(obj);
        jmethodID m = env_->GetMethodID(cls, name, "()I");
        const int r = m ? env_->CallIntMethod(obj, m) : 0;
        if (env_->ExceptionCheck()) env_->ExceptionClear();
        env_->DeleteLocalRef(cls);
        return r;
    }
    float callFloat(const char* name) {
        jobject obj = app_->activity->javaGameActivity;
        jclass cls = env_->GetObjectClass(obj);
        jmethodID m = env_->GetMethodID(cls, name, "()F");
        const float r = m ? env_->CallFloatMethod(obj, m) : 1.f;
        if (env_->ExceptionCheck()) env_->ExceptionClear();
        env_->DeleteLocalRef(cls);
        return r;
    }
    void haptic(int ms) { callVoid("haptic", "(I)V", static_cast<jint>(ms)); }
    void fatal(const std::string& message) {
        jstring s = env_->NewStringUTF(message.c_str());
        callVoid("showFatalError", "(Ljava/lang/String;)V", s);
        env_->DeleteLocalRef(s);
    }

private:
    android_app* app_;
    JNIEnv* env_ = nullptr;
};

class AndroidHaptics final : public cs::IHaptics {
public:
    explicit AndroidHaptics(JavaBridge& bridge) : bridge_(bridge) {}
    void pulse(int ms) override { bridge_.haptic(ms); }

private:
    JavaBridge& bridge_;
};

// AdMob rewarded ads + UMP consent live in AdsManager.java; CuckooActivity exposes them to native code.
class AndroidAds final : public cs::IAds {
public:
    explicit AndroidAds(JavaBridge& bridge) : bridge_(bridge) {}
    cs::RewardedState rewardedState() override {
        const int s = bridge_.callInt("adsRewardedState"); // AdsManager: 0 loading, 1 ready, 2 unavailable
        return s == 1 ? cs::RewardedState::Ready : s == 2 ? cs::RewardedState::Unavailable : cs::RewardedState::Loading;
    }
    bool showRewarded() override { return bridge_.callBool("adsShowRewarded"); }
    bool consumeReward() override { return bridge_.callBool("adsConsumeReward"); }
    bool privacyOptionsRequired() override { return bridge_.callBool("adsPrivacyOptionsRequired"); }
    void showPrivacyOptions() override { bridge_.callVoid("adsShowPrivacyOptions", "()V"); }

private:
    JavaBridge& bridge_;
};

struct AppState {
    android_app* app = nullptr;
    cs::AndroidWindow window;
    cs::Renderer renderer;
    std::unique_ptr<JavaBridge> java;
    std::unique_ptr<cs::FileStorage> storage;
    std::unique_ptr<AndroidHaptics> haptics;
    std::unique_ptr<AndroidAds> ads;
    cs::audio::Synth synth;
    std::unique_ptr<cs::AndroidAudio> audio;
    std::unique_ptr<cs::Game> game;
    bool rendererReady = false, failed = false, animating = false;
    VkExtent2D lastLogical{0, 0};
    cs::quality::Thermal thermal = cs::quality::Thermal::Nominal;
    int targetFps = cs::quality::FPS;
};

float density(android_app* app) {
    const int dpi = AConfiguration_getDensity(app->config);
    return dpi > 0 ? float(dpi) / 160.f : 1.f;
}

void updateInsets(AppState& s) {
    ARect bars{}, cutout{};
    GameActivity_getWindowInsets(s.app->activity, GAMECOMMON_INSETS_TYPE_SYSTEM_BARS, &bars);
    GameActivity_getWindowInsets(s.app->activity, GAMECOMMON_INSETS_TYPE_DISPLAY_CUTOUT, &cutout);
    const float d = density(s.app);
    if (s.game) s.game->setSafeInsets(float(std::max(bars.top, cutout.top)) / d, float(std::max(bars.bottom, cutout.bottom)) / d);
}

// PowerManager thermal status (0 none, 1 light, 2 moderate, 3 severe, 4+ critical) -> quality level
cs::quality::Thermal thermalFromStatus(int status) {
    if (status >= 4) return cs::quality::Thermal::Critical;
    if (status == 3) return cs::quality::Thermal::Serious;
    if (status == 2) return cs::quality::Thermal::Fair;
    return cs::quality::Thermal::Nominal;
}

// Ask the compositor for a lower display refresh where supported (API 30+), so the GPU isn't driven at 90/120 Hz.
void requestFrameRate(ANativeWindow* window, int fps) {
    using SetFrameRate = int32_t (*)(ANativeWindow*, float, int8_t);
    static auto fn = reinterpret_cast<SetFrameRate>(dlsym(RTLD_DEFAULT, "ANativeWindow_setFrameRate"));
    if (fn && window) fn(window, float(fps), 0 /* ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT */);
}

void applyQuality(AppState& s) {
    s.renderer.setRenderScale(cs::quality::renderScale(s.java->callFloat("displayDensity"), s.thermal));
    s.targetFps = cs::quality::targetFps(s.thermal);
    requestFrameRate(s.app->window, s.targetFps);
}

void onWindowReady(AppState& s) {
    s.window.setWindow(s.app->window);
    s.window.setDensity(density(s.app));
    if (s.failed) return;
    if (!s.rendererReady) {
        std::string error;
#ifdef NDEBUG
        const bool validation = false;
#else
        const bool validation = true; // needs the validation layer packaged in jniLibs
#endif
        s.renderer.setPipelineCachePath(std::string(s.app->activity->internalDataPath) + "/vk_pipeline_cache.bin");
        if (!s.renderer.init(s.window, validation, error)) {
            // Explicit diagnostic exit: explain, then let the user close the activity. Never crash.
            __android_log_print(ANDROID_LOG_FATAL, kTag, "Vulkan unavailable: %s", error.c_str());
            s.renderer.shutdown();
            s.failed = true;
            s.java->fatal("Cuckoo Stack needs Vulkan graphics, which this device could not provide.\n\n" + error);
            return;
        }
        s.rendererReady = true;
    } else if (!s.renderer.onSurfaceCreated(s.window)) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "Surface recreation failed; retrying on next frame");
    }
    applyQuality(s); // also re-requests the frame rate on the new window
    updateInsets(s);
}

void handleCommand(android_app* app, int32_t cmd) {
    auto& s = *static_cast<AppState*>(app->userData);
    switch (cmd) {
    case APP_CMD_INIT_WINDOW: onWindowReady(s); s.animating = true; break;
    case APP_CMD_TERM_WINDOW:
        if (s.rendererReady) s.renderer.onSurfaceLost();
        s.window.setWindow(nullptr);
        s.animating = false;
        break;
    case APP_CMD_GAINED_FOCUS: s.animating = s.app->window != nullptr; break;
    case APP_CMD_RESUME: s.animating = s.app->window != nullptr; if (s.audio) s.audio->start(); break;
    case APP_CMD_LOST_FOCUS: s.animating = false; break;
    case APP_CMD_PAUSE: s.animating = false; if (s.audio) s.audio->stop(); break; // like the web build's visibilitychange
    case APP_CMD_WINDOW_RESIZED: case APP_CMD_CONFIG_CHANGED: case APP_CMD_CONTENT_RECT_CHANGED:
        if (s.rendererReady) s.renderer.requestResize();
        break;
    case APP_CMD_WINDOW_INSETS_CHANGED: updateInsets(s); break;
    default: break;
    }
}

void handleInput(AppState& s) {
    android_input_buffer* input = android_app_swap_input_buffers(s.app);
    if (!input) return;
    const float d = density(s.app);
    for (uint64_t i = 0; i < input->motionEventsCount; ++i) {
        const GameActivityMotionEvent& e = input->motionEvents[i];
        const int action = e.action & AMOTION_EVENT_ACTION_MASK;
        if (action != AMOTION_EVENT_ACTION_DOWN && action != AMOTION_EVENT_ACTION_POINTER_DOWN) continue;
        const int idx = action == AMOTION_EVENT_ACTION_POINTER_DOWN
            ? (e.action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT : 0;
        if (s.game && !s.failed)
            s.game->pressAt(GameActivityPointerAxes_getX(&e.pointers[idx]) / d, GameActivityPointerAxes_getY(&e.pointers[idx]) / d);
    }
    android_app_clear_motion_events(input);
    android_app_clear_key_events(input); // back button falls through to the system
}

} // namespace

extern "C" void android_main(android_app* app) {
    cs::setLogSink([](cs::LogLevel level, const char* msg) {
        __android_log_write(level == cs::LogLevel::Error ? ANDROID_LOG_ERROR : level == cs::LogLevel::Warn ? ANDROID_LOG_WARN : ANDROID_LOG_INFO, kTag, msg);
    });
    AppState s;
    s.app = app;
    s.java = std::make_unique<JavaBridge>(app);
    s.storage = std::make_unique<cs::FileStorage>(std::string(app->activity->internalDataPath) + "/cuckoo-stack.save");
    s.haptics = std::make_unique<AndroidHaptics>(*s.java);
    s.ads = std::make_unique<AndroidAds>(*s.java);
    s.game = std::make_unique<cs::Game>(cs::GameServices{&s.synth, s.storage.get(), s.haptics.get(), s.ads.get()});
    s.audio = std::make_unique<cs::AndroidAudio>(s.synth);
#ifndef NDEBUG
    // debug: `adb shell setprop debug.cuckoo.surge 1` starts a surge 4 m into every run
    {
        char v[PROP_VALUE_MAX] = {};
        if (__system_property_get("debug.cuckoo.surge", v) > 0 && v[0] == '1') s.game->setDebugAutoSurge(true);
    }
#endif
    app->userData = &s;
    app->onAppCmd = handleCommand;

    auto prev = std::chrono::steady_clock::now();
    auto nextThermalCheck = prev;
    auto nextFrame = prev;
    while (!app->destroyRequested) {
        android_poll_source* source = nullptr;
        // block while paused; spin while animating (vsync pacing comes from FIFO present)
        while (ALooper_pollOnce(s.animating ? 0 : -1, nullptr, nullptr, reinterpret_cast<void**>(&source)) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested || s.animating) break;
        }
        if (app->destroyRequested) break;
        handleInput(s);
        if (!s.animating || !s.rendererReady || s.failed) { prev = std::chrono::steady_clock::now(); continue; }

        const VkExtent2D logical = s.renderer.logicalExtent();
        if (logical.width && (logical.width != s.lastLogical.width || logical.height != s.lastLogical.height)) {
            s.lastLogical = logical;
            const float d = density(app);
            s.game->resize(float(logical.width) / d, float(logical.height) / d);
        }
        // heat: poll the thermal status every 2 s and step quality down/up
        if (std::chrono::steady_clock::now() >= nextThermalCheck) {
            nextThermalCheck = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            const cs::quality::Thermal t = thermalFromStatus(s.java->callInt("thermalStatus"));
            if (t != s.thermal) {
                __android_log_print(ANDROID_LOG_INFO, kTag, "Thermal level %d", int(t));
                s.thermal = t;
                applyQuality(s);
            }
        }
        // frame pacing: never render faster than the target, even on 90/120 Hz panels that ignore setFrameRate
        const auto frameTime = std::chrono::microseconds(1000000 / s.targetFps);
        nextFrame = std::max(nextFrame + frameTime, std::chrono::steady_clock::now() - frameTime);
        std::this_thread::sleep_until(nextFrame - std::chrono::microseconds(1500)); // FIFO present absorbs the remainder
        const auto now = std::chrono::steady_clock::now();
        s.game->update(std::chrono::duration<double>(now - prev).count());
        prev = now;
        if (!s.renderer.render(s.game->renderList())) {
            s.failed = true;
            s.java->fatal("The graphics device stopped responding (VK_ERROR_DEVICE_LOST).");
        }
    }
    s.audio.reset();
    s.renderer.shutdown();
}
