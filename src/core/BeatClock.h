// Kick-drum clock that mirrors the web build's music scheduler (126 BPM + tempo follows speed,
// 32-bar arrangement, no kicks during the two-bar build before the drop). Visuals pulse from this clock
// whether or not an audio backend is present; a native audio backend should schedule kicks from it too.
#pragma once

#include <algorithm>
#include <cmath>

namespace cs {

class BeatClock {
public:
    void start() { playing_ = true; party_ = false; step_ = 0; nextT_ = now_ + 0.08; bpm_ = 126.0; }
    // Surge party pattern: kick on every beat plus the last 16th of the bar (mirrors Synth::schedulePartyStep)
    void setParty(bool on) { if (on && !party_) partyStep_ = 0; party_ = on; }
    void stop() { playing_ = false; }
    void setTempoFromSpeed(double speed, double cap = 1e9) { bpm_ = std::min(cap, 126.0 + std::max(0.0, speed - 8.0) * 3.4); }
    double bpm() const { return bpm_; }

    // advance in real (not slowed) time, like AudioContext.currentTime
    void advance(double realDt) {
        now_ += realDt;
        if (!playing_) return;
        const double sixteenth = 60.0 / bpm_ / 4.0;
        while (nextT_ <= now_) {
            if (party_) {
                const int s = partyStep_ % 16;
                if (s % 4 == 0 || s == 15) lastKick_ = nextT_;
                partyStep_++;
            } else {
                const int s = step_ % 16, bar = (step_ / 16) % 32;
                const bool buildDrop = bar >= 22 && bar < 24; // section C, bars 22-23
                if (s % 4 == 0 && !buildDrop) lastKick_ = nextT_;
                step_++;
            }
            nextT_ += sixteenth;
        }
    }

    // music.pulse(): exp(-(now - lastKick) * 7), 0 once faded and stopped
    float pulse() const {
        if (lastKick_ < 0) return 0.f;
        const double k = std::exp(-(now_ - lastKick_) * 7.0);
        if (k < 0.01 && !playing_) return 0.f;
        return static_cast<float>(k);
    }

private:
    bool playing_ = false, party_ = false;
    int step_ = 0, partyStep_ = 0;
    double now_ = 0, nextT_ = 0, bpm_ = 126.0, lastKick_ = -1;
};

} // namespace cs
