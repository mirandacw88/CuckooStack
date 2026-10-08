// Android audio output via Oboe: AAudio on API 27+, OpenSL ES on 24-26 (minSdk 24 predates usable AAudio).
// Low-latency float stereo stream; the data callback pulls the synth. Reopens itself when the device
// disconnects (headphones, Bluetooth) and stops while the app is paused.
#pragma once

#include <memory>

namespace cs::audio { class Synth; }

namespace cs {

class AndroidAudio {
public:
    explicit AndroidAudio(audio::Synth& synth);
    ~AndroidAudio();
    void start();
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cs
