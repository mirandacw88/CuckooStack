// The one miniaudio implementation in the build: MP3 decoding for the soundtrack (Music.cpp) on every platform,
// plus the playback device on desktop (DesktopAudio.cpp, CS_MA_DEVICE_IO). Mobile platforms play through
// AVAudioEngine / Oboe, so device I/O is compiled out there.
#define MINIAUDIO_IMPLEMENTATION
#if !defined(CS_MA_DEVICE_IO)
#define MA_NO_DEVICE_IO
#endif
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION
#define MA_NO_WAV
#define MA_NO_FLAC
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <miniaudio.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include "Mp3.h"

#include <new>

namespace cs::audio {

namespace {
struct Impl {
    ma_dr_mp3 dec{};
    ma_dr_mp3_seek_point seek[512];
};
} // namespace

bool mp3Open(Mp3& m, const void* data, size_t size) {
    Impl* d = new (std::nothrow) Impl();
    if (!d || !ma_dr_mp3_init_memory(&d->dec, data, size, nullptr)) { delete d; return false; }
    m.impl = d;
    m.channels = d->dec.channels;
    m.sampleRate = d->dec.sampleRate;
    m.frames = ma_dr_mp3_get_pcm_frame_count(&d->dec);
    ma_uint32 n = 512;
    if (ma_dr_mp3_calculate_seek_points(&d->dec, &n, d->seek)) ma_dr_mp3_bind_seek_table(&d->dec, n, d->seek);
    ma_dr_mp3_seek_to_pcm_frame(&d->dec, 0);
    return true;
}

void mp3Close(Mp3& m) {
    if (!m.impl) return;
    Impl* d = static_cast<Impl*>(m.impl);
    ma_dr_mp3_uninit(&d->dec);
    delete d;
    m.impl = nullptr;
}

uint64_t mp3Read(Mp3& m, uint64_t frames, float* out) {
    return m.impl ? ma_dr_mp3_read_pcm_frames_f32(&static_cast<Impl*>(m.impl)->dec, frames, out) : 0;
}

bool mp3Seek(Mp3& m, uint64_t frame) {
    return m.impl && ma_dr_mp3_seek_to_pcm_frame(&static_cast<Impl*>(m.impl)->dec, frame);
}

} // namespace cs::audio
