// CSEngineView (EngineHost.h): a CAMetalLayer view, the renderer (the game's, IOSWindow's MoltenVK surface), a
// display link, and a Game playing the editor's level from a catalog of its own (svc.levels): the player's touches go
// to it, or the bot plays (Game::debugSetBot), several updates a frame when fast-forwarding.
#include "../../core/Campaign.h"
#include "../../core/Game.h"
#include "../../core/LevelCatalog.h"
#include "../../core/MemoryServices.h"
#include "../../core/audio/Synth.h"
#include "../../graphics/Renderer.h"
#include "../ios/IOSAudio.h"
#include "../ios/IOSWindow.h"

#import "EngineHost.h"
#import <QuartzCore/CAMetalLayer.h>

#include <memory>
#include <string>
#include <vector>

namespace {

class Haptics final : public cs::IHaptics {
public:
    void pulse(int ms) override {
        UIImpactFeedbackGenerator* g = [[UIImpactFeedbackGenerator alloc]
            initWithStyle:ms >= 40 ? UIImpactFeedbackStyleHeavy : ms >= 15 ? UIImpactFeedbackStyleMedium : UIImpactFeedbackStyleLight];
        [g impactOccurred];
    }
};

} // namespace

@implementation CSEngineView {
    CADisplayLink* _link;
    std::unique_ptr<cs::IOSWindow> _window;
    cs::Renderer _renderer;
    BOOL _ready, _failed;
    cs::audio::Synth _synth;
    std::unique_ptr<cs::IOSAudio> _audio;
    Haptics _haptics;
    // the run (the game last: it reads the others)
    std::unique_ptr<cs::LevelCatalog> _cat;
    std::unique_ptr<cs::MemoryStorage> _storage;
    cs::FixedClock _clock; // (the morning the storage's drop was claimed: no daily reward or new-day screens in a play-test)
    std::unique_ptr<cs::Game> _game;
    cs::Game::PlayTest _test;
    CFTimeInterval _last;
    NSMutableArray<NSValue*>* _trace;
    int _frames;
    cs::Game::State _was;
    NSString* _graphicsError;
}

+ (Class)layerClass { return [CAMetalLayer class]; }

+ (instancetype)shared {
    static CSEngineView* view;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ view = [[CSEngineView alloc] initWithFrame:CGRectMake(0, 0, 320, 480)]; });
    return view;
}

- (instancetype)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.multipleTouchEnabled = YES;
        self.backgroundColor = UIColor.blackColor;
        self.contentScaleFactor = UIScreen.mainScreen.nativeScale;
        CAMetalLayer* layer = (CAMetalLayer*)self.layer;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
        _window = std::make_unique<cs::IOSWindow>((__bridge void*)layer);
        _audio = std::make_unique<cs::IOSAudio>(_synth);
        _speed = 1;
        _trace = [NSMutableArray array];
        _graphicsError = @"";
        // (away from the app: no frames at all)
        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(pause) name:UIApplicationWillResignActiveNotification object:nil];
        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(resume) name:UIApplicationDidBecomeActiveNotification object:nil];
    }
    return self;
}

- (NSString*)graphicsError { return _graphicsError; }

- (UIEdgeInsets)gameInsets {
    return UIEdgeInsetsEqualToEdgeInsets(_phoneInsets, UIEdgeInsetsZero) ? self.safeAreaInsets : _phoneInsets;
}

- (void)setPhoneInsets:(UIEdgeInsets)insets {
    _phoneInsets = insets;
    [self setNeedsLayout];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CAMetalLayer* layer = (CAMetalLayer*)self.layer;
    const CGFloat scale = self.contentScaleFactor;
    if (self.bounds.size.width < 1 || self.bounds.size.height < 1) return;
    layer.drawableSize = CGSizeMake(self.bounds.size.width * scale, self.bounds.size.height * scale);
    if (_game) {
        _game->resize(float(self.bounds.size.width), float(self.bounds.size.height));
        _game->setSafeInsets(float([self gameInsets].top), float([self gameInsets].bottom));
    }
    if (_ready) _renderer.requestResize();
}

- (void)didMoveToWindow {
    [super didMoveToWindow];
    if (self.window) [self resume];
    else [self pause];
}

- (BOOL)startGraphics {
    if (_ready) return YES;
    if (_failed) return NO;
    [self layoutIfNeeded];
    std::string error;
    // (the validation layers check every Vulkan call: slow; only when asked for, `--vk-validation`)
    const bool validation = [NSProcessInfo.processInfo.arguments containsObject:@"--vk-validation"];
    NSString* caches = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES).firstObject;
    _renderer.setPipelineCachePath(std::string([caches stringByAppendingPathComponent:@"vk_pipeline_cache.bin"].fileSystemRepresentation));
    if (!_renderer.init(*_window, validation, error)) {
        _renderer.shutdown();
        _failed = YES;
        _graphicsError = [NSString stringWithUTF8String:error.c_str()];
        return NO;
    }
    _ready = YES;
    _audio->start();
    return YES;
}

- (BOOL)playLevel:(int)number text:(NSString*)text grids:(NSDictionary<NSString*, NSString*>*)grids startX:(double)startX bot:(BOOL)bot
            error:(NSString* _Nullable*)error {
    cs::Game::PlayTest t;
    t.level = number;
    t.x = startX;
    return [self play:number text:text grids:grids test:t door:NO bot:bot error:error];
}

- (BOOL)playMaze:(int)number text:(NSString*)text grids:(NSDictionary<NSString*, NSString*>*)grids col:(int)col row:(int)row dir:(int)dir
             bot:(BOOL)bot error:(NSString* _Nullable*)error {
    cs::Game::PlayTest t;
    t.level = number;
    t.col = col;
    t.row = row;
    t.dir = dir;
    return [self play:number text:text grids:grids test:t door:col < 0 bot:bot error:error];
}

- (BOOL)play:(int)number text:(NSString*)text grids:(NSDictionary<NSString*, NSString*>*)grids test:(cs::Game::PlayTest)test door:(BOOL)door
         bot:(BOOL)bot error:(NSString* _Nullable*)error {
    auto cat = std::make_unique<cs::LevelCatalog>();
    std::string why;
    for (NSString* name in grids) {
        std::vector<std::string> rows;
        for (NSString* line in [grids[name] componentsSeparatedByString:@"\n"])
            if (line.length && ![line hasPrefix:@";"]) rows.push_back(line.UTF8String);
        if (!cat->setGauntlet(name.UTF8String, rows, &why)) {
            if (error) *error = [NSString stringWithFormat:@"The %@: %s", name, why.c_str()];
            return NO;
        }
    }
    if (!cat->setLevel(number, text.UTF8String, &why)) {
        if (error) *error = [NSString stringWithUTF8String:why.c_str()];
        return NO;
    }
    if (![self startGraphics]) {
        if (error) *error = [@"The graphics engine didn't start: " stringByAppendingString:_graphicsError];
        return NO;
    }
    _game.reset(); // (before what it reads)
    _cat = std::move(cat);
    _storage = std::make_unique<cs::MemoryStorage>(cs::MemoryStorage::ready());
    cs::Campaign(_storage.get(), _cat.get()).debugSetLevel(number);
    cs::GameServices svc;
    svc.audio = &_synth; svc.storage = _storage.get(); svc.haptics = &_haptics; svc.levels = _cat.get(); svc.clock = &_clock;
    _game = std::make_unique<cs::Game>(svc);
    _test = test;
    _game->debugPlayTest(_test);
    if (door) _game->debugNearGauntlet(true); // (every run from its maze's door)
    _bot = bot;
    _level = number;
    [self setNeedsLayout];
    [self layoutIfNeeded];
    _game->resize(float(self.bounds.size.width), float(self.bounds.size.height));
    _game->setSafeInsets(float([self gameInsets].top), float([self gameInsets].bottom));
    [self startRun];
    [self resume];
    return YES;
}

- (void)startRun {
    if (!_game) return;
    _game->debugSetBot({_bot, false, false, 0});
    for (int i = 0; i < 4 && _game->state() != cs::Game::State::Play; ++i) { _game->press(); _game->update(1.0 / 60.0); } // (past the title, or game over)
    [_trace removeAllObjects];
    _frames = 0;
    _was = _game->state();
    _cleared = NO;
}

- (void)restart { [self startRun]; }

- (void)setBot:(BOOL)bot {
    _bot = bot;
    if (_game) _game->debugSetBot({bot, false, false, 0});
}

- (void)setSpeed:(NSInteger)speed { _speed = MAX(1, MIN(8, speed)); }

- (NSInteger)runState {
    if (!_game) return 0;
    return _game->state() == cs::Game::State::Play ? 1 : _game->state() == cs::Game::State::Dead ? 2 : 0;
}

- (double)henX { return _game ? _game->hen().nodes[size_t(_game->hen().root)].pos.x : 0; }
- (double)henY { return _game ? _game->henBase() : 0; }
- (NSInteger)eggs { return _game ? _game->eggCount() : 0; }
- (double)seconds { return _frames / 60.0; }
- (NSArray<NSValue*>*)trace { return [_trace copy]; }

- (void)resume {
    if (!_ready || _link || !self.window || !_game || UIApplication.sharedApplication.applicationState == UIApplicationStateBackground) return;
    _last = CACurrentMediaTime();
    _link = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
    _link.preferredFrameRateRange = CAFrameRateRangeMake(30, 60, 60);
    [_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    _audio->resume();
}

- (void)pause {
    [_link invalidate];
    _link = nil;
    if (_audio) _audio->pause();
}

- (void)tick:(CADisplayLink*)link {
    if (!_game) return;
    const double dt = std::min(0.05, link.timestamp - _last);
    _last = link.timestamp;
    for (NSInteger i = 0; i < _speed; ++i) {
        _game->update(dt);
        if (_game->state() == cs::Game::State::Play) {
            if (++_frames % 5 == 0)
                [_trace addObject:[NSValue valueWithCGPoint:CGPointMake(self.henX, self.henY + _game->eggCount() * cs::U)]];
        }
        if (_was == cs::Game::State::Play && _game->state() != cs::Game::State::Play) {
            _cleared = _game->lastRunCleared();
            if (self.onRunEnded) self.onRunEnded(_cleared, self.henX, self.henY);
        }
        if (_was != _game->state()) { // (a run under way: 60 fps; its end screen, or before it: 30)
            const float fps = _game->state() == cs::Game::State::Play ? 60 : 30;
            link.preferredFrameRateRange = CAFrameRateRangeMake(fps / 2, fps, fps);
        }
        _was = _game->state();
    }
    if (!_renderer.render(_game->renderList())) {
        [self pause];
        _ready = NO;
        _failed = YES;
        _graphicsError = @"The graphics device stopped responding.";
    }
}

- (void)forward:(NSSet<UITouch*>*)touches phase:(cs::TouchPhase)phase {
    if (!_game || _bot) return;
    for (UITouch* t in touches) {
        const CGPoint p = [t locationInView:self];
        _game->touch(phase, float(p.x), float(p.y));
    }
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self forward:touches phase:cs::TouchPhase::Began]; }
- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self forward:touches phase:cs::TouchPhase::Moved]; }
- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self forward:touches phase:cs::TouchPhase::Ended]; }
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self forward:touches phase:cs::TouchPhase::Cancelled]; }

- (void)dealloc {
    [self pause];
    _game.reset();
    _renderer.shutdown();
}

@end
