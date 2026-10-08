// iOS audio output: AVAudioEngine + AVAudioSourceNode pulling the synth on the real-time render thread.
// Session category Playback, like the web build's `navigator.audioSession.type = 'playback'` (sound plays with
// the ring/silent switch on). Pauses in the background and recovers from interruptions and route changes.
#pragma once

namespace cs::audio { class Synth; }

namespace cs {

class IOSAudio {
public:
    explicit IOSAudio(audio::Synth& synth) : synth_(synth) {}
    ~IOSAudio();
    bool start();
    void pause();   // app resigning active (the web build suspends the AudioContext when hidden)
    void resume();

private:
    audio::Synth& synth_;
    void* impl_ = nullptr; // CSAudioImpl*
};

} // namespace cs
