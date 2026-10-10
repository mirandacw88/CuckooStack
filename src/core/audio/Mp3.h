// A streaming MP3 decoder (miniaudio's dr_mp3, implemented in MiniaudioImpl.cpp, whose declarations only exist in
// the implementation translation unit). Interleaved float output; seeks use a seek table, so jumps are cheap.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cs::audio {

struct Mp3 {
    void* impl = nullptr;
    unsigned channels = 0, sampleRate = 0;
    uint64_t frames = 0;
};

bool mp3Open(Mp3& m, const void* data, size_t size); // decodes headers once, builds the seek table
void mp3Close(Mp3& m);
uint64_t mp3Read(Mp3& m, uint64_t frames, float* out);
bool mp3Seek(Mp3& m, uint64_t frame);

} // namespace cs::audio
