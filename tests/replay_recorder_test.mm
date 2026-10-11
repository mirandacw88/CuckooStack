// The iOS Share Replay video recorder (src/platform/ios/IOSVideoRecorder.mm) on macOS: same code, same VideoToolbox
// hardware encoder. Feeds it frames in real time (the recorder stamps them with the clock) and reads back the MP4s it
// writes: gameplay only (pauses cut out), a revived crash cut, a new run starting a fresh clip.
#include "platform/ios/IOSVideoRecorder.h"

#import <AVFoundation/AVFoundation.h>
#import <QuartzCore/QuartzCore.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

constexpr uint32_t W = 320, H = 568; // small: the test is about timing, not pixels
std::vector<uint8_t> pixels(size_t(W) * H * 4, 90);
int frameNo = 0;

// ~30 fps for `seconds`, each frame a little different so the encoder has something to do
void play(cs::IOSVideoRecorder& r, double seconds) {
    const double end = CACurrentMediaTime() + seconds;
    while (CACurrentMediaTime() < end) {
        std::fill(pixels.begin() + long((frameNo % H) * W * 4), pixels.begin() + long(((frameNo % H) + 1) * W * 4), uint8_t(frameNo * 7));
        r.push(pixels.data(), W, H);
        ++frameNo;
        usleep(33333);
    }
}
void settle() { usleep(250000); } // the encoder's last frames come back asynchronously

struct Clip { int frames = 0; double first = 0, last = 0, maxGap = 0; bool firstKey = false, rising = true; };
Clip read(const std::string& path) {
    Clip c;
    AVURLAsset* asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:@(path.c_str())] options:nil];
    __block AVAssetTrack* track = nil;
    dispatch_semaphore_t loaded = dispatch_semaphore_create(0);
    [asset loadTracksWithMediaType:AVMediaTypeVideo completionHandler:^(NSArray<AVAssetTrack*>* tracks, NSError*) {
        track = tracks.firstObject;
        dispatch_semaphore_signal(loaded);
    }];
    dispatch_semaphore_wait(loaded, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
    if (!track) return c;
    AVAssetReader* reader = [AVAssetReader assetReaderWithAsset:asset error:nil];
    AVAssetReaderTrackOutput* out = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:track outputSettings:nil];
    [reader addOutput:out];
    [reader startReading];
    double prev = -1;
    while (CMSampleBufferRef sb = [out copyNextSampleBuffer]) {
        if (CMSampleBufferGetNumSamples(sb) > 0) {
            const double t = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sb));
            if (c.frames == 0) {
                c.first = t;
                CFArrayRef att = CMSampleBufferGetSampleAttachmentsArray(sb, false);
                CFBooleanRef notSync = nullptr;
                c.firstKey = !(att && CFArrayGetCount(att) > 0 &&
                               CFDictionaryGetValueIfPresent(static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(att, 0)), kCMSampleAttachmentKey_NotSync,
                                                             reinterpret_cast<const void**>(&notSync)) && notSync && CFBooleanGetValue(notSync));
            }
            if (prev >= 0) { c.rising = c.rising && t > prev; if (t - prev > c.maxGap && getenv("CS_GAPS")) std::printf("  gap %.3f at %.3f (frame %d)\n", t - prev, t, c.frames); c.maxGap = std::max(c.maxGap, t - prev); }
            prev = c.last = t;
            ++c.frames;
        }
        CFRelease(sb);
    }
    return c;
}
} // namespace

int main() {
    @autoreleasepool {
        const std::string path = "replay_recorder_test.mp4";
        cs::IOSVideoRecorder r;
        if (!r.start(int(W), int(H))) {
            std::printf("skip: no H.264 encoder here\n");
            return 0;
        }
        check(r.writeLast(15.0, path) == 0, "nothing recorded yet: no clip");

        // a run: 1 s of play, a 0.6 s pause (a menu), 1 s more. (Like the game, capture is paused whenever the test
        // stops to look at the clip: capture on always means frames are coming.)
        r.setCapturing(true);
        play(r, 1.0);
        r.setCapturing(false);
        usleep(600000);
        play(r, 0.3); // frames while paused are ignored
        r.setCapturing(true);
        play(r, 1.0);
        r.setCapturing(false);
        settle();
        double len = r.writeLast(15.0, path);
        Clip c = read(path);
        check(std::abs(len - 2.0) < 0.25, "gameplay only: 2 s of play, the pause cut out");
        check(c.frames > 50 && c.firstKey && c.rising, "the clip: ~30 fps, starts on a keyframe, timestamps rise");
        check(c.maxGap < 0.2, "no hole where the pause was"); // (a 0.6 s pause would show; timing jitter won't)

        // the last N seconds only
        len = r.writeLast(1.0, path);
        check(len >= 1.0 && len <= 2.2, "the last second, from the keyframe before it");

        // a crash, then a revive: the crash is cut out
        r.setCapturing(true);
        play(r, 0.5);
        r.markCrash();
        play(r, 0.8);           // the crash's tail
        r.setCapturing(false);  // the Continue panel
        usleep(400000);
        r.dropSinceMark();      // revived
        r.setCapturing(true);
        play(r, 0.8);
        r.setCapturing(false);
        settle();
        len = r.writeLast(15.0, path);
        c = read(path);
        check(std::abs(len - 3.3) < 0.3, "revive: the crash and the Continue panel are cut (2.5 s + 0.8 s of play)");
        check(c.rising && c.maxGap < 0.2, "revive: the clip carries straight on");

        // a new run: its clip doesn't reach into the last one
        usleep(300000);
        r.resetWindow();
        r.setCapturing(true);
        play(r, 1.0);
        r.setCapturing(false);
        settle();
        len = r.writeLast(15.0, path);
        c = read(path);
        check(std::abs(len - 1.0) < 0.2 && c.firstKey, "a new run: a fresh clip, from its own keyframe");

        // back from the background mid-run (frames stop while capture is on): a fresh session, and the clip still works
        r.setCapturing(true);
        play(r, 0.3);
        usleep(800000);
        play(r, 1.0);
        r.setCapturing(false);
        settle();
        len = r.writeLast(15.0, path);
        check(len > 0.7 && len < 1.3, "after the background: the encoder restarts and records");

        r.stop();
        check(r.writeLast(15.0, path) == 0, "stopped: no clip");
        std::remove(path.c_str());
    }
    std::printf(failures ? "replay_recorder TESTS FAILED\n" : "all replay_recorder checks passed\n");
    return failures ? 1 : 0;
}
