// Offline render of the native synth: the full 32-bar music loop (as it plays during a run, tempo rising with
// speed) followed by every sound effect, written to a 16-bit mono WAV. Also prints per-section loudness so the
// arrangement (filtered intro A, stabs B, build C, drop D) can be checked without listening.
//   render_audio out.wav [seconds]
#include "core/audio/Synth.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "synth.wav";
    const float seconds = argc > 2 ? float(std::atof(argv[2])) : 66.f;
    const int sr = 48000;
    cs::audio::Synth synth{float(sr)};
    std::vector<float> mix;
    std::vector<float> block(480);
    auto run = [&](float secs) { for (int i = 0; i < int(secs * 100); ++i) { synth.render(block.data(), 480, 1); mix.insert(mix.end(), block.begin(), block.end()); } };

    synth.musicStart();
    const float musicSecs = seconds - 6.f;
    for (float t = 0; t < musicSecs; t += 0.5f) { synth.musicTempo(126.f + std::min(4.4f, t * 0.05f) * 3.4f); run(0.5f); }
    synth.musicStop(true); // death: power-down
    run(1.5f);
    const cs::Sfx all[] = {cs::Sfx::Lay, cs::Sfx::Crack, cs::Sfx::Perfect, cs::Sfx::Corn, cs::Sfx::Land, cs::Sfx::Squawk, cs::Sfx::Empty};
    for (cs::Sfx s : all) { synth.play(s, 3); run(0.6f); }

    // stats
    double peak = 0; size_t nans = 0, clipped = 0;
    for (float v : mix) { if (!std::isfinite(v)) nans++; peak = std::max(peak, double(std::fabs(v))); if (std::fabs(v) >= 0.999f) clipped++; }
    std::printf("samples %zu, peak %.3f, clipped %zu, NaN %zu\n", mix.size(), peak, clipped, nans);
    const double bar = 60.0 / 126.0 * 4; // first loop at ~126 BPM
    const char* names[] = {"A intro (bars 0-7)", "B stabs (8-15)", "C build (16-23)", "D drop (24-31)"};
    for (int sec = 0; sec < 4; ++sec) {
        const size_t a = size_t(sec * 8 * bar * sr), b = std::min(mix.size(), size_t((sec + 1) * 8 * bar * sr));
        double sum = 0; for (size_t i = a; i < b; ++i) sum += double(mix[i]) * mix[i];
        std::printf("  %-20s RMS %6.1f dBFS\n", names[sec], 10 * std::log10(sum / double(b - a) + 1e-12));
    }

    FILE* f = std::fopen(path, "wb");
    if (!f) return 1;
    const uint32_t bytes = uint32_t(mix.size() * 2);
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); w32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    w32(16); w16(1); w16(1); w32(sr); w32(sr * 2); w16(2); w16(16);
    std::fwrite("data", 1, 4, f); w32(bytes);
    for (float v : mix) w16(uint16_t(int16_t(std::lround(std::clamp(v, -1.f, 1.f) * 32767))));
    std::fclose(f);
    return nans ? 1 : 0;
}
