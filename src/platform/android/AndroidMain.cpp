// Android entry point (GameActivity + native_app_glue): lifecycle, touch input, storage, haptics and the
// explicit diagnostic exit for devices whose Vulkan loader or driver cannot run the game.
#include "AndroidAudio.h"
#include "AndroidReplay.h"
#include "AndroidWindow.h"
#include "../../core/FileStorage.h"
#include "../../core/Game.h"
#include "../../core/audio/Synth.h"
#include "../../graphics/Quality.h"
#include "../../graphics/Renderer.h"

#include <android/log.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/system_properties.h>
#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <jni.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
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
    bool callBoolStr(const char* name, const char* arg) {
        jstring a = env_->NewStringUTF(arg);
        jobject obj = app_->activity->javaGameActivity;
        jclass cls = env_->GetObjectClass(obj);
        jmethodID m = env_->GetMethodID(cls, name, "(Ljava/lang/String;)Z");
        const bool r = m && env_->CallBooleanMethod(obj, m, a);
        if (env_->ExceptionCheck()) env_->ExceptionClear();
        env_->DeleteLocalRef(cls);
        env_->DeleteLocalRef(a);
        return r;
    }
    double callDoubleStr(const char* name, const char* arg) {
        jstring a = env_->NewStringUTF(arg);
        jobject obj = app_->activity->javaGameActivity;
        jclass cls = env_->GetObjectClass(obj);
        jmethodID m = env_->GetMethodID(cls, name, "(Ljava/lang/String;)D");
        const double r = m ? env_->CallDoubleMethod(obj, m, a) : std::nan("");
        if (env_->ExceptionCheck()) env_->ExceptionClear();
        env_->DeleteLocalRef(cls);
        env_->DeleteLocalRef(a);
        return r;
    }
    // String-returning, no arguments; empty when the method returned null
    std::string callString(const char* name) {
        jobject obj = app_->activity->javaGameActivity;
        jclass cls = env_->GetObjectClass(obj);
        jmethodID m = env_->GetMethodID(cls, name, "()Ljava/lang/String;");
        std::string out;
        if (m) {
            auto js = static_cast<jstring>(env_->CallObjectMethod(obj, m));
            if (js) {
                const char* c = env_->GetStringUTFChars(js, nullptr);
                out = c;
                env_->ReleaseStringUTFChars(js, c);
                env_->DeleteLocalRef(js);
            }
        }
        if (env_->ExceptionCheck()) env_->ExceptionClear();
        env_->DeleteLocalRef(cls);
        return out;
    }
    void callVoidStr(const char* name, const std::string& a, const std::string& b) {
        jstring ja = env_->NewStringUTF(a.c_str()), jb = env_->NewStringUTF(b.c_str());
        callVoid(name, "(Ljava/lang/String;Ljava/lang/String;)V", ja, jb);
        env_->DeleteLocalRef(ja);
        env_->DeleteLocalRef(jb);
    }
    void callVoidStrInt(const char* name, const std::string& a, int n) {
        jstring ja = env_->NewStringUTF(a.c_str());
        callVoid(name, "(Ljava/lang/String;I)V", ja, static_cast<jint>(n));
        env_->DeleteLocalRef(ja);
    }
    void callVoidStr(const char* name, const std::string& a) {
        jstring ja = env_->NewStringUTF(a.c_str());
        callVoid(name, "(Ljava/lang/String;)V", ja);
        env_->DeleteLocalRef(ja);
    }
    void event(const std::string& name, const cs::AnalyticsParams& params) {
        jclass str = env_->FindClass("java/lang/String");
        jobjectArray arr = env_->NewObjectArray(jsize(params.size() * 2), str, nullptr);
        for (size_t i = 0; i < params.size(); ++i) {
            jstring k = env_->NewStringUTF(params[i].first.c_str()), v = env_->NewStringUTF(params[i].second.c_str());
            env_->SetObjectArrayElement(arr, jsize(i * 2), k);
            env_->SetObjectArrayElement(arr, jsize(i * 2 + 1), v);
            env_->DeleteLocalRef(k);
            env_->DeleteLocalRef(v);
        }
        jstring n = env_->NewStringUTF(name.c_str());
        callVoid("fbEvent", "(Ljava/lang/String;[Ljava/lang/String;)V", n, arr);
        env_->DeleteLocalRef(n);
        env_->DeleteLocalRef(arr);
        env_->DeleteLocalRef(str);
    }
    void reminders(const std::vector<cs::Reminder>& r) {
        const jsize n = jsize(r.size());
        jintArray ids = env_->NewIntArray(n);
        jlongArray at = env_->NewLongArray(n);
        jclass str = env_->FindClass("java/lang/String");
        jobjectArray titles = env_->NewObjectArray(n, str, nullptr), bodies = env_->NewObjectArray(n, str, nullptr);
        for (jsize i = 0; i < n; ++i) {
            const jint id = r[size_t(i)].id;
            const jlong t = r[size_t(i)].at;
            env_->SetIntArrayRegion(ids, i, 1, &id);
            env_->SetLongArrayRegion(at, i, 1, &t);
            jstring a = env_->NewStringUTF(r[size_t(i)].title.c_str()), b = env_->NewStringUTF(r[size_t(i)].body.c_str());
            env_->SetObjectArrayElement(titles, i, a);
            env_->SetObjectArrayElement(bodies, i, b);
            env_->DeleteLocalRef(a);
            env_->DeleteLocalRef(b);
        }
        callVoid("notifReplaceAll", "([I[J[Ljava/lang/String;[Ljava/lang/String;)V", ids, at, titles, bodies);
        env_->DeleteLocalRef(ids); env_->DeleteLocalRef(at); env_->DeleteLocalRef(titles); env_->DeleteLocalRef(bodies); env_->DeleteLocalRef(str);
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
    bool showRewarded(const char* placement) override { return bridge_.callBoolStr("adsShowRewarded", placement); }
    bool consumeReward() override { return bridge_.callBool("adsConsumeReward"); }
    bool privacyOptionsRequired() override { return bridge_.callBool("adsPrivacyOptionsRequired"); }
    void showPrivacyOptions() override { bridge_.callVoid("adsShowPrivacyOptions", "()V"); }
    bool interstitialReady() override { return bridge_.callBool("adsInterstitialReady"); }
    bool showInterstitial() override { return bridge_.callBool("adsShowInterstitial"); }
    bool adShowing() override { return bridge_.callBool("adsShowing"); }
    void setAudience(bool child) override { bridge_.callVoid("adsSetAudience", "(Z)V", static_cast<jboolean>(child)); }

private:
    JavaBridge& bridge_;
};

// Firebase Analytics + Remote Config (FirebaseBridge.java)
class AndroidAnalytics final : public cs::IAnalytics, public cs::IRemoteConfig {
public:
    explicit AndroidAnalytics(JavaBridge& bridge) : bridge_(bridge) {}
    void event(const std::string& name, const cs::AnalyticsParams& params) override { bridge_.event(name, params); }
    void userProperty(const std::string& name, const std::string& value) override { bridge_.callVoidStr("fbUserProperty", name, value); }
    std::optional<double> number(const std::string& key) override {
        const double v = bridge_.callDoubleStr("fbRemoteNumber", key.c_str());
        return std::isnan(v) ? std::nullopt : std::optional<double>(v);
    }

private:
    JavaBridge& bridge_;
};

// Reminders + campaign pushes (NotificationsManager.java)
class AndroidNotifications final : public cs::INotifications {
public:
    explicit AndroidNotifications(JavaBridge& bridge) : bridge_(bridge) {}
    cs::NotifPermission permission() override {
        const int p = bridge_.callInt("notifPermission");
        return p == 1 ? cs::NotifPermission::Granted : p == 2 ? cs::NotifPermission::Denied : cs::NotifPermission::Unknown;
    }
    void requestPermission() override { bridge_.callVoid("notifRequestPermission", "()V"); }
    void replaceAll(const std::vector<cs::Reminder>& r) override { bridge_.reminders(r); }
    bool consumeOpened(int& id) override {
        const int o = bridge_.callInt("notifConsumeOpened");
        if (o < 0) return false;
        id = o;
        return true;
    }

private:
    JavaBridge& bridge_;
};

// Daily leaderboard (LeaderboardsManager.java, Play Games Services v2)
class AndroidLeaderboards final : public cs::ILeaderboards {
public:
    explicit AndroidLeaderboards(JavaBridge& bridge) : bridge_(bridge) {}
    bool available() override {
        if (known_ < 0) known_ = bridge_.callBool("boardsAvailable") ? 1 : 0; // fixed for the process; checked every frame
        return known_ == 1;
    }
    void submit(int meters) override { bridge_.callVoid("boardsSubmit", "(I)V", static_cast<jint>(meters)); }
    void show() override { bridge_.callVoid("boardsShow", "()V"); }

private:
    JavaBridge& bridge_;
    int known_ = -1;
};

// Friend nudges (FirebaseBridge.java -> firebase/functions/src/challenges.ts)
class AndroidBackend final : public cs::IBackend {
public:
    explicit AndroidBackend(JavaBridge& bridge) : bridge_(bridge) {}
    bool nudgesAvailable() override { return bridge_.callBool("nudgesAvailable"); }
    void createChallenge(const std::string& day, int meters) override { bridge_.callVoidStrInt("nudgeCreate", day, meters); }
    bool pollChallengeId(std::string& id) override { id = bridge_.callString("nudgePollId"); return !id.empty(); }
    void challengeBeaten(const std::string& id, int meters) override { bridge_.callVoidStrInt("nudgeBeaten", id, meters); }

private:
    JavaBridge& bridge_;
};

// Google Play Billing (StoreManager.java)
class AndroidStore final : public cs::IStore {
public:
    explicit AndroidStore(JavaBridge& bridge) : bridge_(bridge) {}
    std::vector<cs::Product> products() override {
        std::vector<cs::Product> out;
        const std::string all = bridge_.callString("storeProducts"); // "id|price;id|price"
        size_t start = 0;
        while (start < all.size()) {
            const size_t end = std::min(all.find(';', start), all.size());
            const std::string item = all.substr(start, end - start);
            const size_t bar = item.find('|');
            if (bar != std::string::npos) out.push_back({item.substr(0, bar), item.substr(bar + 1)});
            start = end + 1;
        }
        return out;
    }
    bool purchase(const std::string& id) override { return bridge_.callBoolStr("storePurchase", id.c_str()); }
    void restore() override { bridge_.callVoid("storeRestore", "()V"); }
    bool pollEvent(cs::PurchaseEvent& e) override {
        const std::string ev = bridge_.callString("storePollEvent"); // "id|result|restored"
        if (ev.empty()) return false;
        const size_t a = ev.find('|'), b = ev.find('|', a + 1);
        if (a == std::string::npos || b == std::string::npos) return false;
        e.productId = ev.substr(0, a);
        e.result = cs::PurchaseResult(std::clamp(std::atoi(ev.substr(a + 1, b - a - 1).c_str()), 0, 3));
        e.restored = ev.substr(b + 1) == "1";
        return true;
    }

private:
    JavaBridge& bridge_;
};

// The display's vsync, from AChoreographer (looked up at runtime: API 24+, frameCallback64 on 29+): the timestamp of
// the latest refresh and the refresh period. Frames are started on that grid, so at 30 fps on a 60 Hz panel every
// frame spans exactly two refreshes (timing frames from our own clock instead drifts against the panel: some frames
// land on one refresh, some on three - visible stutter).
struct Vsync {
    int64_t last = 0;            // ns, CLOCK_MONOTONIC (= steady_clock on Android)
    double period = 1e9 / 60.0;  // ns, measured
    void* choreographer = nullptr;
    void (*post64)(void*, void (*)(int64_t, void*), void*) = nullptr;
    void (*post)(void*, void (*)(long, void*), void*) = nullptr;
    void start() {
        auto getInstance = reinterpret_cast<void* (*)()>(dlsym(RTLD_DEFAULT, "AChoreographer_getInstance"));
        post64 = reinterpret_cast<decltype(post64)>(dlsym(RTLD_DEFAULT, "AChoreographer_postFrameCallback64"));
        post = reinterpret_cast<decltype(post)>(dlsym(RTLD_DEFAULT, "AChoreographer_postFrameCallback"));
        choreographer = getInstance ? getInstance() : nullptr; // (on this thread's looper)
        request();
    }
    void request() {
        if (!choreographer) return;
        if (post64) post64(choreographer, [](int64_t t, void* self) { static_cast<Vsync*>(self)->tick(t); }, this);
        else if (post) post(choreographer, [](long t, void* self) { static_cast<Vsync*>(self)->tick(int64_t(t)); }, this);
    }
    void tick(int64_t t) {
        if (last > 0 && t > last) { // refine the period from the gap (a whole number of refreshes)
            const double d = double(t - last), n = std::round(d / period);
            if (n >= 1 && n <= 8) period += (d / n - period) * 0.1;
        }
        last = t;
        request(); // every refresh
    }
    bool ready() const { return last > 0; }
};

// ADPF performance hints (Android 12+, looked up at runtime): each frame the game thread reports how long its work
// took against the frame budget, so the scheduler raises CPU clocks / moves it to faster cores as soon as frames run
// long (e.g. while the system wakes background apps), instead of reacting late. No-op on older devices.
struct PerfHint {
    void* session = nullptr;
    int64_t target = 0;
    int (*update)(void*, int64_t) = nullptr;
    int (*report)(void*, int64_t) = nullptr;
    void (*close)(void*) = nullptr;
    void start(int64_t targetNs) {
        auto getManager = reinterpret_cast<void* (*)()>(dlsym(RTLD_DEFAULT, "APerformanceHint_getManager"));
        auto create = reinterpret_cast<void* (*)(void*, const int32_t*, size_t, int64_t)>(dlsym(RTLD_DEFAULT, "APerformanceHint_createSession"));
        update = reinterpret_cast<decltype(update)>(dlsym(RTLD_DEFAULT, "APerformanceHint_updateTargetWorkDuration"));
        report = reinterpret_cast<decltype(report)>(dlsym(RTLD_DEFAULT, "APerformanceHint_reportActualWorkDuration"));
        close = reinterpret_cast<decltype(close)>(dlsym(RTLD_DEFAULT, "APerformanceHint_closeSession"));
        void* manager = getManager ? getManager() : nullptr;
        const int32_t tid = int32_t(gettid());
        session = manager && create && update && report ? create(manager, &tid, 1, targetNs) : nullptr;
        target = targetNs;
        __android_log_print(ANDROID_LOG_INFO, "CuckooStack", "Performance hints: %s", session ? "on" : "unavailable");
    }
    void setTarget(int64_t ns) { if (session && ns != target) { update(session, ns); target = ns; } }
    void frame(int64_t workNs) { if (session && workNs > 0) report(session, workNs); }
    ~PerfHint() { if (session && close) close(session); }
};

struct AppState {
    android_app* app = nullptr;
    cs::AndroidWindow window;
    cs::Renderer renderer;
    std::unique_ptr<JavaBridge> java;
    std::unique_ptr<cs::FileStorage> storage;
    std::unique_ptr<AndroidHaptics> haptics;
    std::unique_ptr<AndroidAds> ads;
    std::unique_ptr<AndroidAnalytics> analytics;
    std::unique_ptr<AndroidStore> store;
    std::unique_ptr<AndroidNotifications> notifications;
    std::unique_ptr<cs::AndroidReplay> replay;
    std::unique_ptr<AndroidLeaderboards> leaderboards;
    std::unique_ptr<AndroidBackend> backend;
    std::chrono::steady_clock::time_point pausedAt{};
    cs::audio::Synth synth;
    std::unique_ptr<cs::AndroidAudio> audio;
    std::unique_ptr<cs::Game> game;
    bool rendererReady = false, failed = false, animating = false;
    VkExtent2D lastLogical{0, 0};
    cs::quality::Thermal thermal = cs::quality::Thermal::Nominal;
    int targetFps = cs::quality::FPS;
    cs::quality::FramePacer pacer; // 60, or an even 30 when this phone can't keep 60
    Vsync vsync;
    PerfHint perf;
    std::chrono::steady_clock::time_point lastFrameEnd = std::chrono::steady_clock::now();
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
// The display's refresh rate for this window. FIXED_SOURCE: the content really is fps (a game locked to it), so the
// display may switch to a matching mode (30 Hz for an even 30 fps; at 60 Hz, 30 fps frames from our own clock land
// on 1, 2 or 3 refreshes and stutter). ALWAYS (API 31+): switch even if it costs a visible mode change.
void requestFrameRate(ANativeWindow* window, int fps) {
    if (!window) return;
    using WithStrategy = int32_t (*)(ANativeWindow*, float, int8_t, int8_t);
    using SetFrameRate = int32_t (*)(ANativeWindow*, float, int8_t);
    static auto withStrategy = reinterpret_cast<WithStrategy>(dlsym(RTLD_DEFAULT, "ANativeWindow_setFrameRateWithChangeStrategy"));
    static auto fn = reinterpret_cast<SetFrameRate>(dlsym(RTLD_DEFAULT, "ANativeWindow_setFrameRate"));
    constexpr int8_t kFixedSource = 1, kAlways = 1; // ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE, ..._CHANGE_FRAME_RATE_ALWAYS
    if (withStrategy) withStrategy(window, float(fps), kFixedSource, kAlways);
    else if (fn) fn(window, float(fps), kFixedSource);
}

void applyQuality(AppState& s) {
    s.renderer.setRenderScale(cs::quality::renderScale(s.java->callFloat("displayDensity"), s.thermal));
    s.targetFps = std::min(cs::quality::targetFps(s.thermal), s.pacer.fps());
    requestFrameRate(s.app->window, s.targetFps);
    s.perf.setTarget(1'000'000'000LL / s.targetFps); // the frame budget
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
            s.java->fatal("Cuckoo Stack OVERDRIVE needs Vulkan graphics, which this device could not provide.\n\n" + error);
            return;
        }
#ifndef NDEBUG
        s.renderer.setGpuTiming(true); // debug: GPU ms per pass in the log every 2 s
#endif
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
        if (s.replay) s.replay->onSurfaceLost();
        if (s.rendererReady) s.renderer.onSurfaceLost();
        s.window.setWindow(nullptr);
        s.animating = false;
        break;
    case APP_CMD_GAINED_FOCUS: s.animating = s.app->window != nullptr; break;
    case APP_CMD_RESUME:
        s.animating = s.app->window != nullptr;
        if (s.audio) s.audio->start();
        // back after a long break counts as a new session (analytics, daily drop, streak prompt)
        if (s.game && s.pausedAt.time_since_epoch().count() && std::chrono::steady_clock::now() - s.pausedAt > std::chrono::minutes(30)) s.game->onForeground();
        break;
    case APP_CMD_LOST_FOCUS: s.animating = false; break;
    case APP_CMD_PAUSE: // like the web build's visibilitychange
        s.animating = false;
        if (s.audio) s.audio->stop();
        s.pausedAt = std::chrono::steady_clock::now();
        if (s.game) s.game->onBackground();
        break;
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
    s.analytics = std::make_unique<AndroidAnalytics>(*s.java);
    s.store = std::make_unique<AndroidStore>(*s.java);
    s.notifications = std::make_unique<AndroidNotifications>(*s.java);
    s.leaderboards = std::make_unique<AndroidLeaderboards>(*s.java);
    s.backend = std::make_unique<AndroidBackend>(*s.java);
    {
        JavaBridge* java = s.java.get();
        cs::AndroidReplay::Platform rp;
        rp.share = [java](const std::string& path, const std::string& text) { java->callVoidStr("replayShare", path, text); };
        rp.pollSharedTarget = [java] { return java->callString("replayPollShared"); };
        s.replay = std::make_unique<cs::AndroidReplay>(s.renderer, std::string(app->activity->internalDataPath) + "/../cache", 48000, std::move(rp));
    }
    cs::GameServices services;
    services.audio = &s.synth; services.storage = s.storage.get(); services.haptics = s.haptics.get(); services.ads = s.ads.get();
    services.analytics = s.analytics.get(); services.remoteConfig = s.analytics.get(); services.store = s.store.get();
    services.notifications = s.notifications.get();
    services.replay = s.replay.get();
    services.leaderboards = s.leaderboards.get();
    services.backend = s.backend.get();
    s.game = std::make_unique<cs::Game>(services);
    s.pacer.remembered(s.game->lite()); // found low-end on an earlier launch: lite + 30 fps from the first frame
    if (s.pacer.decided()) s.java->callVoid("splashReady", "()V");
    s.audio = std::make_unique<cs::AndroidAudio>(s.synth);
    s.audio->setTap([](void* ctx, const float* samples, int frames, int channels) { static_cast<cs::AndroidReplay*>(ctx)->pushAudio(samples, frames, channels); },
                    s.replay.get());
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
    s.vsync.start();
    s.perf.start(1'000'000'000LL / s.targetFps);
    int64_t frameVsync = 0; // the refresh the last frame was started on
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
        // frame pacing: never render faster than the target, even on 90/120 Hz panels that ignore setFrameRate. With
        // the display's vsync, frames start on its refresh grid, every k-th refresh (60 Hz: k = 1 for 60 fps, 2 for
        // 30), so each frame is on screen for the same number of refreshes; without it (very old devices), our clock.
        if (s.vsync.ready()) {
            const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            frameVsync = cs::quality::nextFrameVsync(frameVsync, s.vsync.last, s.vsync.period, s.targetFps, nowNs); // (Quality.h)
            std::this_thread::sleep_until(std::chrono::steady_clock::time_point(std::chrono::nanoseconds(frameVsync + 1000000))); // 1 ms after it
        } else {
            const auto frameTime = std::chrono::microseconds(1000000 / s.targetFps);
            nextFrame = std::max(nextFrame + frameTime, std::chrono::steady_clock::now() - frameTime);
            std::this_thread::sleep_until(nextFrame - std::chrono::microseconds(1500)); // FIFO present absorbs the remainder
        }
        const auto now = std::chrono::steady_clock::now();
        if (const std::string link = s.java->callString("linkPoll"); !link.empty()) s.game->openLink(link);
        const auto t0 = std::chrono::steady_clock::now();
        s.game->update(std::chrono::duration<double>(now - prev).count());
        prev = now;
        const auto t1 = std::chrono::steady_clock::now();
        if (s.replay) { s.replay->setAudioRate(s.audio ? s.audio->sampleRate() : 48000); s.replay->frame(s.thermal >= cs::quality::Thermal::Critical); } // recording is light: only Critical stops it
        if (!s.renderer.render(s.game->renderList())) {
            s.failed = true;
            s.java->fatal("The graphics device stopped responding (VK_ERROR_DEVICE_LOST).");
        }
        {   // pacing: an even 30 fps when this phone's GPU can't keep 60 (alternating 16 / 33 ms frames stutter)
            const auto end = std::chrono::steady_clock::now();
            const double busyMs = std::chrono::duration<double, std::milli>(end - t0).count();
            s.perf.frame(std::chrono::duration_cast<std::chrono::nanoseconds>(end - t0).count()); // this frame's work
            const double dt = std::chrono::duration<double>(end - s.lastFrameEnd).count();
            s.lastFrameEnd = end;
            const bool wasDecided = s.pacer.decided();
            if (dt < 0.5 && s.pacer.frame(busyMs, dt)) { // (a long gap: back from the background, not a slow frame)
                __android_log_print(ANDROID_LOG_INFO, kTag, "Low-end device: lite effects, %d fps", s.pacer.fps());
                s.game->setLite(true);
                applyQuality(s);
            }
            if (!wasDecided && s.pacer.decided()) s.java->callVoid("splashReady", "()V"); // the splash may go now
        }
#ifndef NDEBUG
        { // debug: slow frames. A frame that took over 30 ms (normal: 5-10) logs what the last three frames drew: the GPU
            // works a frame or two behind, so the heavy one is often before the frame that waited for it.
            static std::string recent[3];
            static int at = 0;
            recent[at] = s.game->frameSummary();
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (ms > 30.0) {
                __android_log_print(ANDROID_LOG_WARN, kTag, "SLOW FRAME %.1f ms", ms);
                for (int k = 2; k >= 0; --k)
                    __android_log_print(ANDROID_LOG_WARN, kTag, "  frame -%d: %s", k, recent[(at + 3 - k) % 3].c_str());
            }
            at = (at + 1) % 3;
            // ... and every frame's summary with its start time (CLOCK_MONOTONIC, the clock SurfaceFlinger's present
            // timestamps use) to <files>/frames.log, to match against the display's hitches afterwards
            static FILE* log = std::fopen((std::string(s.app->activity->internalDataPath) + "/frames.log").c_str(), "w");
            if (log) {
                std::fprintf(log, "%lld %.1f %s\n", (long long)std::chrono::duration_cast<std::chrono::nanoseconds>(t0.time_since_epoch()).count(), ms,
                             recent[(at + 2) % 3].c_str());
                static int lines = 0;
                if (++lines % 30 == 0) std::fflush(log);
            }
        }
        { // debug: CPU time per frame (game update / render submit incl. waiting for a free frame), every 2 s
            static double up = 0, rn = 0, worst = 0; static int frames = 0;
            static auto logAt = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            const auto t2 = std::chrono::steady_clock::now();
            const double u = std::chrono::duration<double, std::milli>(t1 - t0).count(), r = std::chrono::duration<double, std::milli>(t2 - t1).count();
            up += u; rn += r; worst = std::max(worst, u + r); ++frames;
            if (t2 >= logAt) {
                __android_log_print(ANDROID_LOG_INFO, kTag, "CPU ms/frame: update %.2f  render %.2f  worst %.2f  (%d frames)", up / frames, rn / frames, worst, frames);
                up = rn = worst = 0; frames = 0; logAt = t2 + std::chrono::seconds(2);
            }
        }
#endif
    }
    s.audio.reset();   // no more audio taps
    s.replay.reset();  // encoders + the recorder swapchain go before the renderer
    s.renderer.shutdown();
}
