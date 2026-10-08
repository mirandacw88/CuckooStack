#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <miniaudio.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include "DesktopAudio.h"
#include "../../core/Log.h"
#include "../../core/audio/Synth.h"

namespace cs {

namespace {
void callback(ma_device* device, void* output, const void*, ma_uint32 frames) {
    static_cast<audio::Synth*>(device->pUserData)->render(static_cast<float*>(output), int(frames), int(device->playback.channels));
}
} // namespace

bool DesktopAudio::start(audio::Synth& synth) {
    auto* device = new ma_device;
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = 0; // device native rate
    cfg.dataCallback = callback;
    cfg.pUserData = &synth;
    if (ma_device_init(nullptr, &cfg, device) != MA_SUCCESS) {
        CS_LOGW("No audio output device; running silently");
        delete device;
        return false;
    }
    synth.setSampleRate(float(device->sampleRate));
    if (ma_device_start(device) != MA_SUCCESS) {
        ma_device_uninit(device);
        delete device;
        return false;
    }
    device_ = device;
    CS_LOGI("Audio: %u Hz, %u ch (%s)", device->sampleRate, device->playback.channels, ma_get_backend_name(device->pContext->backend));
    return true;
}

void DesktopAudio::stop() {
    if (!device_) return;
    auto* device = static_cast<ma_device*>(device_);
    ma_device_uninit(device);
    delete device;
    device_ = nullptr;
}

} // namespace cs
