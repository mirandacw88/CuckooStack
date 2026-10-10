// The recorded soundtrack (assets/music, embedded): one track on the title screen and during runs. The MP3 is
// decoded while it plays (no full-length PCM in memory), resampled to the device rate and mixed in stereo under the
// synth's sound effects (Synth::render).
//
// Behaviour: start() fades the track in where it last stopped; a surge speeds it up (and, like a fast-forwarded
// tape, raises its pitch) and eases it back afterwards; stop(tapeStop) winds the music down like a tape stopping
// (a crash) or just fades it. The beat grid (MusicTracks.inc, from scripts/audio/analyze_music.py) lets the game's
// visuals pulse in time (beat()).
//
// Threading: the control methods and render() run on the audio thread (Synth forwards its commands); beat() may be
// read from any thread.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace cs::audio {

struct TrackInfo { double bpm, firstBeat, loudest, length; };
const TrackInfo* musicTrackInfo(int track); // nullptr if the soundtrack isn't built in

class MusicPlayer {
public:
    MusicPlayer();
    ~MusicPlayer();
    bool ok() const { return ok_; }

    // audio thread
    void start();
    void stop(bool tapeStop);
    void surge(bool on);
    void setMuted(bool muted) { muted_ = muted; }
    void render(float* out, int frames, int channels, float outRate); // adds into interleaved `out`

    // any thread: the track you hear (-1: none), the position in it (seconds) and its playback rate
    void beat(int& track, double& seconds, double& rate) const;

private:
    struct Track;
    float advance(Track& t, float ratio, float& l, float& r);
    std::vector<std::unique_ptr<Track>> tracks_;
    bool ok_ = false, playing_ = false, muted_ = false, surging_ = false;
    float master_ = 0.f, masterTarget_ = 0.f, rate_ = 1.f;   // master fade; tape-stop rate
    float surgeRate_ = 1.f;                                  // playback speed: eases up to kSurgeRate in a surge
    bool tapeStopping_ = false;
    std::atomic<int> beatTrack_{-1};
    std::atomic<double> beatPos_{0.0}, beatRate_{1.0};
};

} // namespace cs::audio
