// The recorded soundtrack (assets/music, embedded): track 0 plays on the title and during runs, track 1 during a
// surge. The MP3s are decoded while they play (no full-length PCM in memory), resampled to the device rate and
// mixed in stereo under the synth's sound effects (Synth::render).
//
// Behaviour: start() fades the run track in where it last stopped; a surge crossfades to the surge track's loudest
// section, then back to the run track exactly where it left off; stop(tapeStop) winds the music down like a tape
// stopping (a crash) or just fades it. Beat grids (MusicTracks.inc, from scripts/audio/analyze_music.py) let the
// game's visuals pulse in time (beat()).
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

    // any thread: the track you hear (-1: none) and the position in it, seconds
    void beat(int& track, double& seconds) const;

private:
    struct Track;
    float advance(Track& t, float ratio, float& l, float& r);
    std::vector<std::unique_ptr<Track>> tracks_;
    bool ok_ = false, playing_ = false, muted_ = false, surging_ = false;
    float master_ = 0.f, masterTarget_ = 0.f, rate_ = 1.f;   // master fade; playback rate (tape stop)
    bool tapeStopping_ = false;
    std::atomic<int> beatTrack_{-1};
    std::atomic<double> beatPos_{0.0};
};

} // namespace cs::audio
