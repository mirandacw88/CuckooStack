// Desktop audio output (miniaudio): pulls the synth from the device callback.
#pragma once

namespace cs::audio { class Synth; }

namespace cs {

class DesktopAudio {
public:
    ~DesktopAudio() { stop(); }
    bool start(audio::Synth& synth); // false: no device (the game keeps running silently)
    void stop();

private:
    void* device_ = nullptr; // ma_device*
};

} // namespace cs
