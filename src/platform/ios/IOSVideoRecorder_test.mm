// The iOS Share Replay video recorder (src/platform/ios/IOSVideoRecorder.mm) on macOS: same code, same VideoToolbox
// hardware encoder. Feeds it frames in real time (the recorder stamps them with the clock) and reads back the MP4s it
// writes: gameplay only (pauses cut out), a revived crash cut, a new run starting a fresh clip.
#include "platform/ios/IOSVideoRecorder.h"

#import <AVFoundation/AVFoundation.h>
#import <QuartzCore/QuartzCore.h>
#import <VideoToolbox/VideoToolbox.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
int failures = 0;
bool hardware = true; // a real hardware H.264 encoder (every iPhone / iPad / Mac), not a CI machine

// Is there a hardware H.264 encoder? Without one encoding is slow enough to drop frames, so the checks on frame rate
// and clip length - the machine's speed, not the recorder's logic - are skipped. CI machines (GitHub's virtual Macs:
// the CI environment variable) skip them too: some offer a virtualised "hardware" encoder that still drops frames.
bool hasHardwareEncoder() {
    NSDictionary* spec = @{(id)kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder: @YES};
    VTCompressionSessionRef s = nullptr;
    const OSStatus st = VTCompressionSessionCreate(nullptr, 320, 568, kCMVideoCodecType_H264, (__bridge CFDictionaryRef)spec, nullptr, nullptr,
                                                   nullptr, nullptr, &s);
    if (s) { VTCompressionSessionInvalidate(s); CFRelease(s); }
    return st == noErr;
}
void check(bool ok, const char* what);
// speed-dependent checks: only meaningful with a hardware encoder
void timed(bool ok, const char* what) {
    if (hardware) check(ok, what);
    else std::printf("skip  %s (timing depends on this machine's encoder)\n", what);
}
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
        const bool ci = std::getenv("CI") != nullptr;
        hardware = !ci && hasHardwareEncoder();
        std::printf("H.264 encoder: %s\n", hardware ? "hardware" : ci ? "CI machine (speed checks skipped)" : "software only (speed checks skipped)");
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
        check(len > 0.5 && len < 2.4, "gameplay only: the 0.6 s pause is cut out of the clip");
        timed(std::abs(len - 2.0) < 0.25, "gameplay only: exactly 2 s of play");
        // (Frame rate and gaps depend on the machine: CI's virtual Macs encode in software at ~15 fps and the recorder
        // skips frames rather than fall behind. So these check the logic: a cut-out pause would leave a 0.6 s hole.)
        check(c.frames >= 2 && c.firstKey && c.rising, "the clip: starts on a keyframe, timestamps rise");
        timed(c.frames >= 20, "the clip: at least 10 fps");
        timed(c.maxGap < 0.45, "no hole where the pause was"); // (a slow encoder drops frames: gaps of its own)

        // the last N seconds only
        len = r.writeLast(1.0, path);
        check(len > 0 && len <= 2.2, "the last second, from the keyframe before it");
        timed(len >= 1.0, "the last second: at least a full second");

        // a crash, then a revive: the crash is cut out. Measured against this machine: how long the clip was before,
        // and how long each stretch really played (a slow encoder makes every push, so every play(), run long)
        const double before = r.writeLast(15.0, path);
        auto timedPlay = [&](double s) { const double t0 = CACurrentMediaTime(); play(r, s); return CACurrentMediaTime() - t0; };
        r.setCapturing(true);
        double kept = timedPlay(0.5);
        r.markCrash();
        const double tail = timedPlay(0.8); // the crash's tail
        r.setCapturing(false);  // the Continue panel
        usleep(400000);
        r.dropSinceMark();      // revived
        r.setCapturing(true);
        kept += timedPlay(0.8);
        r.setCapturing(false);
        settle();
        len = r.writeLast(15.0, path);
        c = read(path);
        check(len > before && len - before < kept + tail / 2,
              "revive: the crash and the Continue panel are cut (the clip grows by the play kept, not the crash's tail)");
        timed(std::abs(len - 3.3) < 0.3, "revive: exactly 2.5 s + 0.8 s of play");
        check(c.rising, "revive: timestamps keep rising across the cut");
        timed(c.maxGap < 0.45, "revive: the clip carries straight on (the cut was 1.2 s)");

        // a new run: its clip doesn't reach into the last one
        usleep(300000);
        r.resetWindow();
        r.setCapturing(true);
        play(r, 1.0);
        r.setCapturing(false);
        settle();
        len = r.writeLast(15.0, path);
        c = read(path);
        check(len > 0 && len < 1.3 && c.firstKey, "a new run: a fresh clip (not the old run's), from its own keyframe");
        timed(std::abs(len - 1.0) < 0.2, "a new run: exactly its 1 s");

        // back from the background mid-run (frames stop while capture is on): a fresh session, and the clip still works
        r.setCapturing(true);
        play(r, 0.3);
        usleep(800000);
        play(r, 1.0);
        r.setCapturing(false);
        settle();
        len = r.writeLast(15.0, path);
        check(len > 0 && len < 1.3, "after the background: the encoder restarts and records");
        timed(len > 0.7, "after the background: most of the second is recorded");

        // a revive's cut at the instant of a frame: the crash marked microseconds before a frame, and the revived run's
        // first frame pushed microseconds after capture resumes. Frame times are stored on a 1/90000 s grid, so that
        // frame and the mark share a tick; it must still be cut, and the revived frame come strictly after what's kept.
        // (Once, against an unrounded mark, the crash's first frame rounded down before it, survived the cut and shared
        // its time with the revived run's first: the clip failed to write.) Twenty times: where in a tick the mark
        // falls is chance.
        r.resetWindow();
        r.setCapturing(true);
        play(r, 0.5);
        int written = 0, rose = 0;
        for (int k = 0; k < 20; ++k) {
            r.markCrash();
            r.push(pixels.data(), W, H); // the crash's first frame, right after the mark
            play(r, 0.1);
            r.setCapturing(false);
            r.dropSinceMark();
            r.setCapturing(true);
            r.push(pixels.data(), W, H); // the revived run's first frame, right as the timeline resumes
            play(r, 0.1);
            r.setCapturing(false);
            settle();
            const double l = r.writeLast(15.0, path);
            const Clip cut = read(path);
            written += l > 0;
            rose += cut.frames > 0 && cut.rising;
            r.setCapturing(true);
        }
        r.setCapturing(false);
        check(written == 20, ("a cut at the instant of a frame: every clip written (" + std::to_string(written) + " of 20)").c_str());
        check(rose == 20, ("...its frames' times strictly rising across the cut (" + std::to_string(rose) + " of 20)").c_str());

        r.stop();
        check(r.writeLast(15.0, path) == 0, "stopped: no clip");
        std::remove(path.c_str());
    }
    std::printf(failures ? "replay_recorder TESTS FAILED\n" : "all replay_recorder checks passed\n");
    return failures ? 1 : 0;
}
