// Share Replay's video on iOS. The renderer's ~30 fps readback frames (Renderer::startReadback) go through the
// hardware H.264 encoder into a rolling ~20 s window of encoded frames, a keyframe every second; writeLast() muxes the
// last seconds into an MP4 without re-encoding. Replaces ReplayKit, whose rolling buffer dropped frames under the
// game's load (the clips smeared until the next keyframe) and which asked for permission on every start.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace cs {

class IOSVideoRecorder {
public:
    IOSVideoRecorder();
    ~IOSVideoRecorder();
    bool start(int width, int height); // main thread
    void stop();
    void push(const uint8_t* bgra, uint32_t width, uint32_t height); // main thread (the renderer's frame sink)
    // only gameplay is recorded: while paused no frames come in, and the paused time is cut out of the timeline
    void setCapturing(bool on);
    void resetWindow(); // a new run: its clip never reaches back into the previous one
    void markCrash();     // a crash: dropSinceMark() cuts from here (a revive)
    void dropSinceMark(); // ... and the clip carries on from the next frame, on a keyframe
    // the last `seconds` (from the keyframe at or before them) as an MP4; blocks, call off the main thread.
    // Returns the clip's length in seconds, 0 on failure.
    double writeLast(double seconds, const std::string& path);

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace cs
