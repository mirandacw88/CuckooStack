#include "IOSAudio.h"
#include "../../core/Log.h"
#include "../../core/audio/Synth.h"

#import <AVFoundation/AVFoundation.h>

@interface CSAudioImpl : NSObject
@property(nonatomic, strong) AVAudioEngine* engine;
@property(nonatomic, strong) AVAudioSourceNode* source;
@property(nonatomic, assign) cs::audio::Synth* synth;
@property(nonatomic, assign) BOOL wantsRunning;
@end

@implementation CSAudioImpl

- (BOOL)setUp {
    AVAudioSession* session = AVAudioSession.sharedInstance;
    NSError* err = nil;
    [session setCategory:AVAudioSessionCategoryPlayback mode:AVAudioSessionModeDefault options:0 error:&err];
    [session setPreferredIOBufferDuration:0.005 error:nil];
    [session setActive:YES error:&err];
    if (err) CS_LOGW("AVAudioSession: %s", err.localizedDescription.UTF8String);

    self.engine = [AVAudioEngine new];
    const double rate = [self.engine.outputNode outputFormatForBus:0].sampleRate;
    self.synth->setSampleRate(float(rate > 0 ? rate : 48000.0));
    AVAudioFormat* format = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:(rate > 0 ? rate : 48000.0) channels:2];
    cs::audio::Synth* synth = self.synth;
    self.source = [[AVAudioSourceNode alloc] initWithFormat:format renderBlock:^OSStatus(BOOL*, const AudioTimeStamp*, AVAudioFrameCount frames, AudioBufferList* out) {
        // standard format is deinterleaved float32: render mono into the first buffer, copy to the rest
        float* left = static_cast<float*>(out->mBuffers[0].mData);
        synth->render(left, int(frames), 1);
        for (UInt32 b = 1; b < out->mNumberBuffers; ++b) memcpy(out->mBuffers[b].mData, left, frames * sizeof(float));
        return noErr;
    }];
    [self.engine attachNode:self.source];
    [self.engine connect:self.source to:self.engine.mainMixerNode format:format];

    NSNotificationCenter* nc = NSNotificationCenter.defaultCenter;
    [nc addObserver:self selector:@selector(interrupted:) name:AVAudioSessionInterruptionNotification object:session];
    [nc addObserver:self selector:@selector(reset:) name:AVAudioSessionMediaServicesWereResetNotification object:session];
    [nc addObserver:self selector:@selector(configChanged:) name:AVAudioEngineConfigurationChangeNotification object:self.engine];
    return YES;
}

- (void)run {
    self.wantsRunning = YES;
    if (self.engine.isRunning) return;
    [AVAudioSession.sharedInstance setActive:YES error:nil];
    NSError* err = nil;
    if (![self.engine startAndReturnError:&err]) CS_LOGW("AVAudioEngine start failed: %s", err.localizedDescription.UTF8String);
    else CS_LOGI("Audio: %.0f Hz (AVAudioEngine, %s)", [self.engine.outputNode outputFormatForBus:0].sampleRate,
                 AVAudioSession.sharedInstance.currentRoute.outputs.firstObject.portType.UTF8String);
}

- (void)halt {
    self.wantsRunning = NO;
    [self.engine pause];
}

- (void)interrupted:(NSNotification*)n {
    const NSUInteger type = [n.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
    if (type == AVAudioSessionInterruptionTypeEnded && self.wantsRunning) [self run];
}

- (void)configChanged:(NSNotification*)n { // headphones, AirPlay, sample-rate change
    if (self.wantsRunning) dispatch_async(dispatch_get_main_queue(), ^{ [self run]; });
}

- (void)reset:(NSNotification*)n { // media services reset: rebuild everything
    [self.engine stop];
    [self setUp];
    if (self.wantsRunning) [self run];
}

- (void)dealloc {
    [NSNotificationCenter.defaultCenter removeObserver:self];
    [self.engine stop];
}
@end

namespace cs {

IOSAudio::~IOSAudio() {
    if (impl_) CFBridgingRelease(impl_);
}

bool IOSAudio::start() {
    CSAudioImpl* impl = [CSAudioImpl new];
    impl.synth = &synth_;
    if (![impl setUp]) return false;
    [impl run];
    impl_ = (void*)CFBridgingRetain(impl);
    return true;
}

void IOSAudio::pause() { if (impl_) [(__bridge CSAudioImpl*)impl_ halt]; }
void IOSAudio::resume() { if (impl_) [(__bridge CSAudioImpl*)impl_ run]; }

} // namespace cs
