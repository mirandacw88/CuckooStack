#include "IOSVideoRecorder.h"
#include "../../core/Log.h"

#import <AVFoundation/AVFoundation.h>
#import <QuartzCore/QuartzCore.h>
#import <VideoToolbox/VideoToolbox.h>

#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

namespace cs {

namespace {
constexpr double kWindow = 20.0;   // seconds of encoded video kept
constexpr int kMaxInFlight = 4;    // frames queued in the encoder; beyond this a frame is skipped (never mid-stream)

bool isKeyframe(CMSampleBufferRef sb) {
    CFArrayRef att = CMSampleBufferGetSampleAttachmentsArray(sb, false);
    if (!att || CFArrayGetCount(att) == 0) return true;
    auto dict = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(att, 0));
    CFBooleanRef notSync = nullptr;
    return !(CFDictionaryGetValueIfPresent(dict, kCMSampleAttachmentKey_NotSync, reinterpret_cast<const void**>(&notSync)) && notSync &&
             CFBooleanGetValue(notSync));
}
double ptsOf(CMSampleBufferRef sb) { return CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sb)); }
} // namespace

struct IOSVideoRecorder::Impl {
    VTCompressionSessionRef session = nullptr;
    int width = 0, height = 0;
    std::mutex m;
    std::deque<CMSampleBufferRef> samples; // retained; presentation = decode order (no frame reordering)
    std::atomic<int> inFlight{0};
    std::atomic<bool> rebuild{false}; // the session died (iOS kills hardware encoders in the background): start over
    double lastPush = -1, retryAt = 0;
    double pausedTotal = 0, pausedAt = -1; // capture pauses, cut out of the clip's timeline
    double markPts = -1;                   // the last crash, on the clip's timeline
    // a revive's cut: frames encoded before it (the generation then) and stamped from the crash on are dropped
    // when the encoder hands them back late. (Not by time alone: the timeline closes over the cut, so the revived
    // run's frames reuse those timestamps.)
    std::atomic<uintptr_t> generation{0};
    std::atomic<double> cutFrom{-1};
    bool forceKey = true;
#ifndef NDEBUG
    double costSec = 0, costLogAt = 0; // debug: time spent in push() (copy + submit), logged every 2 s
    int costFrames = 0;
#endif

    void clear() {
        std::lock_guard<std::mutex> lock(m);
        for (CMSampleBufferRef s : samples) CFRelease(s);
        samples.clear();
    }
    void stop() {
        if (session) {
            VTCompressionSessionCompleteFrames(session, kCMTimeInvalid);
            VTCompressionSessionInvalidate(session);
            CFRelease(session);
            session = nullptr;
        }
        clear();
        inFlight = 0;
    }
    // encoder thread
    void encoded(uintptr_t gen, OSStatus status, VTEncodeInfoFlags flags, CMSampleBufferRef sb) {
        --inFlight;
        if (status != noErr) rebuild = true;
        if (status != noErr || !sb || (flags & kVTEncodeInfo_FrameDropped) || !CMSampleBufferDataIsReady(sb)) return;
        std::lock_guard<std::mutex> lock(m);
        if (gen != generation.load() && ptsOf(sb) >= cutFrom.load()) return; // from the crash a revive cut out
        if (samples.empty() && !isKeyframe(sb)) return; // the window always starts on a keyframe
        CFRetain(sb);
        samples.push_back(sb);
        // drop whole keyframe groups that end before the window
        const double newest = ptsOf(sb);
        for (;;) {
            size_t next = 1;
            while (next < samples.size() && !isKeyframe(samples[next])) ++next;
            if (next >= samples.size() || ptsOf(samples[next]) > newest - kWindow) break;
            for (size_t i = 0; i < next; ++i) CFRelease(samples[i]);
            samples.erase(samples.begin(), samples.begin() + long(next));
        }
    }
};

IOSVideoRecorder::IOSVideoRecorder() : impl_(std::make_unique<Impl>()) {}
IOSVideoRecorder::~IOSVideoRecorder() { impl_->stop(); }

bool IOSVideoRecorder::start(int width, int height) {
    Impl& s = *impl_;
    s.stop();
    NSDictionary* source = @{(id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA), (id)kCVPixelBufferWidthKey: @(width),
                             (id)kCVPixelBufferHeightKey: @(height), (id)kCVPixelBufferIOSurfacePropertiesKey: @{}};
    VTCompressionOutputCallback cb = [](void* ref, void* frameRef, OSStatus status, VTEncodeInfoFlags flags, CMSampleBufferRef sb) {
        static_cast<Impl*>(ref)->encoded(reinterpret_cast<uintptr_t>(frameRef), status, flags, sb);
    };
    OSStatus st = VTCompressionSessionCreate(nullptr, width, height, kCMVideoCodecType_H264, nullptr, (__bridge CFDictionaryRef)source, nullptr, cb, &s,
                                             &s.session);
    if (st != noErr || !s.session) { CS_LOGW("Replay: video encoder unavailable (%d)", int(st)); s.session = nullptr; return false; }
    VTSessionSetProperty(s.session, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);
    VTSessionSetProperty(s.session, kVTCompressionPropertyKey_ProfileLevel, kVTProfileLevel_H264_High_AutoLevel);
    VTSessionSetProperty(s.session, kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse);
    VTSessionSetProperty(s.session, kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration, (__bridge CFNumberRef)@1.0);
    VTSessionSetProperty(s.session, kVTCompressionPropertyKey_MaxKeyFrameInterval, (__bridge CFNumberRef)@30);
    VTSessionSetProperty(s.session, kVTCompressionPropertyKey_ExpectedFrameRate, (__bridge CFNumberRef)@30);
    VTSessionSetProperty(s.session, kVTCompressionPropertyKey_AverageBitRate, (__bridge CFNumberRef)@6000000);
    VTCompressionSessionPrepareToEncodeFrames(s.session);
    s.width = width; s.height = height;
    s.lastPush = -1; s.forceKey = true; s.rebuild = false;
    CS_LOGI("Replay: recording %dx%d (H.264, in-game)", width, height);
    return true;
}

void IOSVideoRecorder::stop() { impl_->stop(); }

void IOSVideoRecorder::resetWindow() {
    impl_->clear();
    impl_->markPts = -1;
    impl_->forceKey = true; // the new window starts on a keyframe
}

void IOSVideoRecorder::markCrash() {
    Impl& s = *impl_;
    s.markPts = (s.pausedAt >= 0 ? s.pausedAt : CACurrentMediaTime()) - s.pausedTotal;
}

void IOSVideoRecorder::dropSinceMark() {
    Impl& s = *impl_;
    if (s.markPts < 0) return;
    const double end = (s.pausedAt >= 0 ? s.pausedAt : CACurrentMediaTime()) - s.pausedTotal; // captured so far
    {
        std::lock_guard<std::mutex> lock(s.m);
        while (!s.samples.empty() && ptsOf(s.samples.back()) >= s.markPts) { CFRelease(s.samples.back()); s.samples.pop_back(); }
    }
    s.cutFrom = s.markPts;
    ++s.generation;
    s.pausedTotal += std::max(0.0, end - s.markPts); // the timeline closes over the cut
    s.forceKey = true; // the next frame can't lean on the dropped ones
    s.markPts = -1;
}

void IOSVideoRecorder::setCapturing(bool on) {
    Impl& s = *impl_;
    const double now = CACurrentMediaTime();
    if (!on && s.pausedAt < 0) s.pausedAt = now;
    if (on && s.pausedAt >= 0) {
        s.pausedTotal += now - s.pausedAt;
        s.pausedAt = -1;
        s.lastPush = -1; // the pause isn't a trip to the background
    }
}

void IOSVideoRecorder::push(const uint8_t* bgra, uint32_t width, uint32_t height) {
    Impl& s = *impl_;
    if (s.width == 0 || int(width) != s.width || int(height) != s.height) return;
    if (s.pausedAt >= 0) return; // a frame read back just before the pause: not gameplay any more
    const double now = CACurrentMediaTime();
    // back from the background (iOS invalidated the encoder) or an encode error: a fresh session and window
    const bool away = s.lastPush >= 0 && now - s.lastPush > 0.5;
    if (away || s.rebuild || !s.session) {
        if (now < s.retryAt) return;
        const bool error = s.rebuild || !s.session;
        if (!start(s.width, s.height)) { s.retryAt = now + 1.0; return; } // (start keeps the size; tried again in a second)
        if (error) CS_LOGW("Replay: video encoder restarted after an error");
    }
    if (s.inFlight.load() >= kMaxInFlight) return; // the encoder is behind: skip this source frame
    s.lastPush = now;
    CVPixelBufferPoolRef pool = VTCompressionSessionGetPixelBufferPool(s.session);
    CVPixelBufferRef pb = nullptr;
    if (!pool || CVPixelBufferPoolCreatePixelBuffer(nullptr, pool, &pb) != kCVReturnSuccess || !pb) return;
    CVPixelBufferLockBaseAddress(pb, 0);
    auto* dst = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(pb));
    const size_t stride = CVPixelBufferGetBytesPerRow(pb), row = size_t(width) * 4;
    if (stride == row) std::memcpy(dst, bgra, row * height); // one copy when the rows are packed alike
    else for (uint32_t y = 0; y < height; ++y) std::memcpy(dst + y * stride, bgra + y * row, row);
    CVPixelBufferUnlockBaseAddress(pb, 0);
    NSDictionary* props = s.forceKey ? @{(id)kVTEncodeFrameOptionKey_ForceKeyFrame: @YES} : nil;
    s.forceKey = false;
    ++s.inFlight;
    if (VTCompressionSessionEncodeFrame(s.session, pb, CMTimeMakeWithSeconds(now - s.pausedTotal, 90000), kCMTimeInvalid, (__bridge CFDictionaryRef)props,
                                        reinterpret_cast<void*>(s.generation.load()), nullptr) != noErr) {
        --s.inFlight;
        s.rebuild = true;
    }
    CVPixelBufferRelease(pb);
#ifndef NDEBUG
    s.costSec += CACurrentMediaTime() - now;
    ++s.costFrames;
    if (now >= s.costLogAt) {
        if (s.costFrames > 0 && s.costLogAt > 0) CS_LOGI("Replay: recorder %.2f ms per frame (%d frames)", s.costSec * 1000.0 / s.costFrames, s.costFrames);
        s.costSec = 0; s.costFrames = 0; s.costLogAt = now + 2.0;
    }
#endif
}

double IOSVideoRecorder::writeLast(double seconds, const std::string& path) {
    Impl& s = *impl_;
    std::vector<CMSampleBufferRef> clip;
    {
        std::lock_guard<std::mutex> lock(s.m);
#ifndef NDEBUG
        if (!s.samples.empty())
            CS_LOGI("Replay: window %zu frames, %.2f..%.2f s", s.samples.size(), ptsOf(s.samples.front()), ptsOf(s.samples.back()));
#endif
        if (s.samples.empty()) return 0;
        const double from = ptsOf(s.samples.back()) - seconds;
        size_t start = 0;
        for (size_t i = 0; i < s.samples.size() && ptsOf(s.samples[i]) <= from; ++i)
            if (isKeyframe(s.samples[i])) start = i;
        for (size_t i = start; i < s.samples.size(); ++i) { CFRetain(s.samples[i]); clip.push_back(s.samples[i]); }
    }
    double length = 0;
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        [NSFileManager.defaultManager removeItemAtURL:url error:nil];
        NSError* err = nil;
        AVAssetWriter* writer = [AVAssetWriter assetWriterWithURL:url fileType:AVFileTypeMPEG4 error:&err];
        CMFormatDescriptionRef fmt = CMSampleBufferGetFormatDescription(clip.front());
        AVAssetWriterInput* in = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:nil sourceFormatHint:fmt];
        in.expectsMediaDataInRealTime = NO;
        if (writer && [writer canAddInput:in]) {
            [writer addInput:in];
            const CMTime first = CMSampleBufferGetPresentationTimeStamp(clip.front());
            const CMTime last = CMSampleBufferGetPresentationTimeStamp(clip.back());
            bool ok = [writer startWriting];
            if (ok) [writer startSessionAtSourceTime:first];
            for (size_t i = 0; ok && i < clip.size(); ++i) {
                for (int wait = 0; !in.readyForMoreMediaData && wait < 500; ++wait) usleep(2000);
                ok = [in appendSampleBuffer:clip[i]];
            }
            [in markAsFinished];
            const CMTime end = CMTimeAdd(last, CMTimeMake(1, 30));
            [writer endSessionAtSourceTime:end];
            dispatch_semaphore_t done = dispatch_semaphore_create(0);
            [writer finishWritingWithCompletionHandler:^{ dispatch_semaphore_signal(done); }];
            dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC));
            if (ok && writer.status == AVAssetWriterStatusCompleted) length = CMTimeGetSeconds(CMTimeSubtract(end, first));
            else CS_LOGW("Replay: writing the clip failed (%s)", writer.error.localizedDescription.UTF8String ?: "unknown");
        } else {
            CS_LOGW("Replay: can't write the clip (%s)", err.localizedDescription.UTF8String ?: "no input");
        }
    }
    for (CMSampleBufferRef sb : clip) CFRelease(sb);
    return length;
}

} // namespace cs
