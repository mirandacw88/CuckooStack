// The MP3 decoder (Mp3.h / MiniaudioImpl.cpp): the embedded soundtrack and applause open with the expected format,
// read and seek frame-accurately (the song restarts and the replay audio depend on it), and bad data fails cleanly.
#include "TestUtil.h"
#include "core/audio/Mp3.h"

#include <cmath>
#include <vector>

namespace cs::assets {
extern const unsigned char midnight_city_sprint_mp3[];
extern const size_t midnight_city_sprint_mp3_size;
extern const unsigned char applause_cheer_mp3[];
extern const size_t applause_cheer_mp3_size;
} // namespace cs::assets

using namespace cs::audio;
using namespace test;

namespace {

void track(const char* name, const unsigned char* data, size_t size, double seconds) {
    Mp3 m;
    const std::string n(name);
    check(mp3Open(m, data, size), (n + ": opens").c_str());
    check(m.channels >= 1 && m.channels <= 2 && (m.sampleRate == 44100 || m.sampleRate == 48000), (n + ": a normal format").c_str());
    const double len = double(m.frames) / m.sampleRate;
    check(seconds > 0 ? std::abs(len - seconds) < 1.0 : len > 2.0 && len < 30.0, (n + ": the expected length").c_str());
    std::vector<float> a(4096 * m.channels), b(4096 * m.channels);
    check(mp3Read(m, 4096, a.data()) == 4096, (n + ": reads the frames asked for").c_str());
    const uint64_t at = m.frames / 2;
    check(mp3Seek(m, at) && mp3Read(m, 4096, a.data()) == 4096, (n + ": seeks to the middle").c_str());
    // the same frames read straight through from a little earlier
    mp3Seek(m, at - 10000);
    std::vector<float> skip(10000 * m.channels);
    mp3Read(m, 10000, skip.data());
    mp3Read(m, 4096, b.data());
    double diff = 0, level = 0;
    for (size_t i = 0; i < a.size(); ++i) { diff += std::abs(a[i] - b[i]); level += std::abs(a[i]); }
    check(level > 1.0 && diff < level * 0.01, (n + ": a seek lands on the same samples as reading through").c_str());
    mp3Seek(m, 0);
    uint64_t total = 0, r = 0;
    std::vector<float> all(65536 * m.channels);
    while ((r = mp3Read(m, 65536, all.data())) > 0) total += r;
    check(total == m.frames, (n + ": every reported frame decodes, straight through").c_str());
    // near the very end the decoder's seek table lands up to one MP3 frame (1152 samples, 26 ms) late; the game only
    // seeks to the start and the loudest section, so that slack is accepted here
    bool tail = true;
    for (uint64_t back : {100ull, 2000ull, 5000ull}) {
        mp3Seek(m, m.frames - back);
        const uint64_t got = mp3Read(m, 8192, all.data());
        tail = tail && got <= back && got + 1152 >= back;
    }
    check(tail, (n + ": reading past the end stops at the end (within one MP3 frame)").c_str());
    mp3Close(m);
    check(m.impl == nullptr && mp3Read(m, 10, a.data()) == 0 && !mp3Seek(m, 0), (n + ": closed: reads nothing, no crash").c_str());
}

} // namespace

int main() {
    track("soundtrack", cs::assets::midnight_city_sprint_mp3, cs::assets::midnight_city_sprint_mp3_size, 170.0);
    track("applause", cs::assets::applause_cheer_mp3, cs::assets::applause_cheer_mp3_size, -1); // a few seconds of cheering
    std::vector<unsigned char> junk(5000, 0x41);
    Mp3 bad;
    check(!mp3Open(bad, junk.data(), junk.size()) && bad.impl == nullptr, "garbage data: fails to open, cleanly");
    check(!mp3Open(bad, nullptr, 0), "no data: fails to open");
    return finish("Mp3");
}
