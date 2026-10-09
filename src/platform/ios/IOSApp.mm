// iOS application shell: UIApplication delegate, a UIView backed by CAMetalLayer, CADisplayLink frame loop,
// touch input, safe-area insets, haptics, persistence and an explicit diagnostic alert if Vulkan cannot start.
#include "IOSAds.h"
#include "IOSAudio.h"
#include "IOSServices.h"
#include "IOSWindow.h"
#include "../../core/FileStorage.h"
#include "../../core/Game.h"
#include "../../core/Log.h"
#include "../../core/audio/Synth.h"
#include "../../graphics/Quality.h"
#include "../../graphics/Renderer.h"

#import "CuckooStack-Swift.h"
#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>

#include <memory>
#include <string>

namespace {

class IOSHaptics final : public cs::IHaptics {
public:
    void pulse(int ms) override {
        UIImpactFeedbackStyle style = ms >= 40 ? UIImpactFeedbackStyleHeavy : ms >= 15 ? UIImpactFeedbackStyleMedium : UIImpactFeedbackStyleLight;
        UIImpactFeedbackGenerator* g = [[UIImpactFeedbackGenerator alloc] initWithStyle:style];
        [g impactOccurred];
    }
};

// a link that opened the app (challenge links); the game picks it up on its next frame
std::string gPendingLink;

std::string savePath() {
    NSURL* dir = [[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject;
    [[NSFileManager defaultManager] createDirectoryAtURL:dir withIntermediateDirectories:YES attributes:nil error:nil];
    return std::string([[dir URLByAppendingPathComponent:@"cuckoo-stack.save"].path UTF8String]);
}

} // namespace

@interface CSGameView : UIView
@end
@implementation CSGameView
+ (Class)layerClass { return [CAMetalLayer class]; }
@end

@interface CSGameViewController : UIViewController
@end

@implementation CSGameViewController {
    CADisplayLink* _link;
    std::unique_ptr<cs::IOSWindow> _window;
    std::unique_ptr<cs::FileStorage> _storage;
    IOSHaptics _haptics;
    cs::audio::Synth _synth;
    std::unique_ptr<cs::IOSAudio> _audio;
    cs::IOSAds _ads;
    cs::IOSAnalytics _analytics;
    cs::IOSNotifications _notifications;
    cs::IOSReplay _replay;
    cs::IOSLeaderboards _leaderboards;
    cs::IOSBackend _backend;
    std::unique_ptr<cs::IOSStore> _store;
    std::unique_ptr<cs::Game> _game;
    CFTimeInterval _backgroundAt; // a long break counts as a new session
    cs::Renderer _renderer;
    BOOL _ready;
    UIView* _splash; // opening splash, identical to LaunchScreen.storyboard; nil once dismissed
    CFTimeInterval _last;
}

- (void)loadView {
    CSGameView* view = [[CSGameView alloc] initWithFrame:UIScreen.mainScreen.bounds];
    view.multipleTouchEnabled = YES;
    view.contentScaleFactor = UIScreen.mainScreen.nativeScale;
    ((CAMetalLayer*)view.layer).pixelFormat = MTLPixelFormatBGRA8Unorm;
    ((CAMetalLayer*)view.layer).framebufferOnly = YES;
    self.view = view;

    // Opening splash: same layout as LaunchScreen.storyboard, so launch -> splash is seamless.
    _splash = [[UIView alloc] initWithFrame:view.bounds];
    _splash.backgroundColor = [UIColor colorNamed:@"SplashBackground"]; // = logo background (make_splash.py)
    _splash.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    UIImageView* logo = [[UIImageView alloc] initWithImage:[UIImage imageNamed:@"SplashLogo"]];
    logo.contentMode = UIViewContentModeScaleAspectFit;
    logo.translatesAutoresizingMaskIntoConstraints = NO;
    [_splash addSubview:logo];
    NSLayoutConstraint* preferred = [logo.widthAnchor constraintEqualToAnchor:_splash.widthAnchor multiplier:0.8];
    preferred.priority = UILayoutPriorityRequired - 1;
    // the image's intrinsic size must not win over the 80% / 520 pt rule
    for (UILayoutConstraintAxis ax : {UILayoutConstraintAxisHorizontal, UILayoutConstraintAxisVertical}) {
        [logo setContentHuggingPriority:1 forAxis:ax];
        [logo setContentCompressionResistancePriority:1 forAxis:ax];
    }
    const CGSize sz = logo.image.size;
    [NSLayoutConstraint activateConstraints:@[
        [logo.centerXAnchor constraintEqualToAnchor:_splash.centerXAnchor],
        [logo.centerYAnchor constraintEqualToAnchor:_splash.centerYAnchor],
        [logo.widthAnchor constraintLessThanOrEqualToConstant:520], preferred,
        [logo.widthAnchor constraintLessThanOrEqualToAnchor:_splash.widthAnchor multiplier:0.8],
        [logo.heightAnchor constraintEqualToAnchor:logo.widthAnchor multiplier:(sz.width > 0 ? sz.height / sz.width : 0.33)],
    ]];
    [view addSubview:_splash];
}

- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures { return UIRectEdgeAll; }

- (void)viewDidLoad {
    [super viewDidLoad];
    _storage = std::make_unique<cs::FileStorage>(savePath());
    _store = std::make_unique<cs::IOSStore>();
    cs::GameServices services;
    services.audio = &_synth; services.storage = _storage.get(); services.haptics = &_haptics; services.ads = &_ads;
    services.analytics = &_analytics; services.remoteConfig = &_analytics; services.store = _store.get();
    services.notifications = &_notifications;
    services.replay = &_replay;
    services.leaderboards = &_leaderboards;
    services.backend = &_backend;
    _game = std::make_unique<cs::Game>(services);
#ifndef NDEBUG
    // debug: `xcrun simctl launch <device> com.cuckoostack.aerospheregames --surge` starts a surge 4 m into every run
    if ([NSProcessInfo.processInfo.arguments containsObject:@"--surge"]) _game->setDebugAutoSurge(true);
#endif
    _audio = std::make_unique<cs::IOSAudio>(_synth);
    _audio->start();
    _window = std::make_unique<cs::IOSWindow>((__bridge void*)self.view.layer);
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(pause) name:UIApplicationWillResignActiveNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(resume) name:UIApplicationDidBecomeActiveNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(didEnterBackground) name:UIApplicationDidEnterBackgroundNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(willEnterForeground) name:UIApplicationWillEnterForegroundNotification object:nil];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    CAMetalLayer* layer = (CAMetalLayer*)self.view.layer;
    const CGFloat scale = self.view.contentScaleFactor;
    layer.drawableSize = CGSizeMake(self.view.bounds.size.width * scale, self.view.bounds.size.height * scale);
    _game->resize(float(self.view.bounds.size.width), float(self.view.bounds.size.height));
    _game->setSafeInsets(float(self.view.safeAreaInsets.top), float(self.view.safeAreaInsets.bottom));
    if (_ready) _renderer.requestResize();
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    if (!_ready) {
        std::string error;
#ifdef NDEBUG
        const bool validation = false;
#else
        const bool validation = true;
#endif
        NSString* caches = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES).firstObject;
        _renderer.setPipelineCachePath(std::string([caches stringByAppendingPathComponent:@"vk_pipeline_cache.bin"].fileSystemRepresentation));
        if (!_renderer.init(*_window, validation, error)) {
            _renderer.shutdown();
            [self showFatal:[NSString stringWithUTF8String:error.c_str()]];
            return;
        }
        _ready = YES;
        [self applyQuality];
        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(thermalChanged) name:NSProcessInfoThermalStateDidChangeNotification object:nil];
        // hold the logo a moment once the game is running underneath, then fade to the title screen
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.6 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ [self dismissSplash]; });
    }
    [self resume];
}

- (void)showFatal:(NSString*)detail {
    NSString* msg = [NSString stringWithFormat:@"Cuckoo Stack could not start its graphics engine.\n\n%@", detail];
    UIAlertController* alert = [UIAlertController alertControllerWithTitle:@"Graphics unavailable" message:msg preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (void)resume {
    if (_audio) _audio->resume();
    if (!_ready || _link) return;
    _last = CACurrentMediaTime();
    _link = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
    [self applyFrameRate];
    [_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}

- (void)didEnterBackground {
    _backgroundAt = CACurrentMediaTime();
    if (_game) _game->onBackground();
}

- (void)willEnterForeground {
    if (_game && _backgroundAt > 0 && CACurrentMediaTime() - _backgroundAt > 30 * 60) _game->onForeground();
}

- (void)pause {
    if (_audio) _audio->pause();
    [_link invalidate];
    _link = nil;
}

- (void)tick:(CADisplayLink*)link {
    const CFTimeInterval now = link.timestamp;
    if (!gPendingLink.empty()) { _game->openLink(gPendingLink); gPendingLink.clear(); }
    _game->update(now - _last);
    _last = now;
    if (!_renderer.render(_game->renderList())) {
        [self pause];
        _ready = NO;
        [self showFatal:@"The graphics device stopped responding."];
    }
}

// Heat / battery: internal resolution + frame-rate cap from the screen density and the device's thermal state.
- (cs::quality::Thermal)thermal {
    switch (NSProcessInfo.processInfo.thermalState) {
    case NSProcessInfoThermalStateFair: return cs::quality::Thermal::Fair;
    case NSProcessInfoThermalStateSerious: return cs::quality::Thermal::Serious;
    case NSProcessInfoThermalStateCritical: return cs::quality::Thermal::Critical;
    default: return cs::quality::Thermal::Nominal;
    }
}

- (void)applyFrameRate {
    const int fps = cs::quality::targetFps([self thermal]);
    if (@available(iOS 15.0, *)) _link.preferredFrameRateRange = CAFrameRateRangeMake(fps / 2.f, fps, fps);
    else _link.preferredFramesPerSecond = fps;
}

- (void)applyQuality {
    _renderer.setRenderScale(cs::quality::renderScale(float(self.view.contentScaleFactor), [self thermal]));
    [self applyFrameRate];
}

- (void)thermalChanged {
    dispatch_async(dispatch_get_main_queue(), ^{
        CS_LOGI("Thermal state %ld", (long)NSProcessInfo.processInfo.thermalState);
        if (self->_ready) [self applyQuality];
    });
}

- (void)dismissSplash {
    UIView* splash = _splash;
    if (!splash) return;
    _splash = nil;
    CS_LOGI("Splash dismissed");
    _ads.start(); // consent / tracking prompts only after the splash, never over it
    [UIView animateWithDuration:0.4 animations:^{ splash.alpha = 0; } completion:^(BOOL) { [splash removeFromSuperview]; }];
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    if (_splash) { if (_ready) [self dismissSplash]; return; } // a tap skips the splash; it doesn't start a run
    for (UITouch* t in touches) {
        const CGPoint p = [t locationInView:self.view];
        _game->pressAt(float(p.x), float(p.y));
    }
}

- (void)dealloc {
    [self pause];
    _renderer.shutdown();
}
@end

@interface CSAppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow* window;
@end

@implementation CSAppDelegate
// challenge links: universal links (https://<domain>/c?...) and the custom scheme
- (BOOL)application:(UIApplication*)app openURL:(NSURL*)url options:(NSDictionary<UIApplicationOpenURLOptionsKey, id>*)options {
    gPendingLink = url.absoluteString.UTF8String;
    return YES;
}
- (BOOL)application:(UIApplication*)application continueUserActivity:(NSUserActivity*)activity
    restorationHandler:(void (^)(NSArray<id<UIUserActivityRestoring>>*))restorationHandler {
    if (![activity.activityType isEqualToString:NSUserActivityTypeBrowsingWeb] || !activity.webpageURL) return NO;
    gPendingLink = activity.webpageURL.absoluteString.UTF8String;
    return YES;
}
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
    if (NSURL* url = launchOptions[UIApplicationLaunchOptionsURLKey]) gPendingLink = url.absoluteString.UTF8String;
    [CSFirebase.shared start]; // first: Crashlytics and Analytics want to see the whole launch
    [CSNotifications.shared start]; // before the first frame, so a tap on a reminder that launched the app counts
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [CSGameViewController new];
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char* argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([CSAppDelegate class]));
    }
}
