// The game-audio tap (ReplayAudioTap.h, iOS): gating, mark / rewind, the WAV it writes.
#include "TestUtil.h"
#include "platform/ios/ReplayAudioTap.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace cs;
using namespace test;

namespace {

// ---------------------------------------------------------------- game-audio tap (iOS)

struct Wav { uint32_t rate = 0; uint16_t channels = 0, bits = 0; std::vector<int16_t> pcm; };
Wav readWav(const std::string& path) {
    Wav w;
    std::ifstream f(path, std::ios::binary);
    std::vector<char> b((std::istreambuf_iterator<char>(f)), {});
    if (b.size() < 44 || std::string(b.data(), 4) != "RIFF" || std::string(b.data() + 8, 4) != "WAVE") return w;
    std::memcpy(&w.channels, &b[22], 2); std::memcpy(&w.rate, &b[24], 4); std::memcpy(&w.bits, &b[34], 2);
    uint32_t data = 0;
    std::memcpy(&data, &b[40], 4);
    w.pcm.resize(std::min<size_t>(data, b.size() - 44) / 2);
    std::memcpy(w.pcm.data(), &b[44], w.pcm.size() * 2);
    return w;
}
void pushRamp(ReplayAudioTap& tap, int frames, float value) {
    std::vector<float> buf(512 * 2, value);
    for (int done = 0; done < frames; done += 512) tap.push(buf.data(), std::min(512, frames - done));
}

void audioTap() {
    const std::string path = "replay_test_audio.wav";
    auto tap = std::make_unique<ReplayAudioTap>(); // (~4 MB once enabled)
    tap->setRate(48000);
    pushRamp(*tap, 4800, 0.5f);
    check(!tap->writeWav(path, 1.0), "audio tap: nothing kept before replays are on");
    tap->enable();
    pushRamp(*tap, 4800, 0.5f);
    check(!tap->writeWav(path, 1.0), "audio tap: nothing kept while capture is paused");
    tap->setCapturing(true);
    pushRamp(*tap, 48000, 0.5f);
    check(tap->writeWav(path, 10.0), "audio tap: writes a WAV");
    Wav w = readWav(path);
    check(w.rate == 48000 && w.channels == 2 && w.bits == 16, "audio tap: 48 kHz, stereo, 16-bit");
    check(w.pcm.size() == size_t(48000) * 2, "audio tap: exactly what was captured (1 s), not more");
    check(!w.pcm.empty() && w.pcm.front() == int16_t(0.5f * 32767.f), "audio tap: the samples themselves");
    tap->writeWav(path, 0.25);
    check(readWav(path).pcm.size() == size_t(12000) * 2, "audio tap: the last N seconds only");
    // a revive: what was kept since the crash goes
    tap->mark();
    pushRamp(*tap, 24000, -0.25f);
    tap->setCapturing(false);
    tap->rewindToMark();
    tap->setCapturing(true);
    pushRamp(*tap, 4800, 2.f); // clipped
    tap->writeWav(path, 10.0);
    w = readWav(path);
    check(w.pcm.size() == size_t(48000 + 4800) * 2, "audio tap: a revive drops what came after the crash");
    bool noCrashAudio = true;
    for (int16_t v : w.pcm) noCrashAudio = noCrashAudio && v != int16_t(-0.25f * 32767.f);
    check(noCrashAudio, "audio tap: none of the cut crash's audio is left");
    check(w.pcm.back() == 32767, "audio tap: loud samples are clipped, not wrapped");
    // it's a rolling window: older audio falls out
    pushRamp(*tap, 48000 * 25, 0.1f);
    tap->writeWav(path, 60.0);
    w = readWav(path);
    check(w.pcm.size() / 2 < size_t(48000) * 20 && w.pcm.size() / 2 > size_t(48000) * 19, "audio tap: keeps about the last 20 s");
    std::remove(path.c_str());
}

} // namespace

int main() {
    audioTap();
    return finish("ReplayAudioTap");
}
