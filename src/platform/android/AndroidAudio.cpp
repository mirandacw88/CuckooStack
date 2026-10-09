#include "AndroidAudio.h"
#include "../../core/Log.h"
#include "../../core/audio/Synth.h"

#include <oboe/Oboe.h>

#include <mutex>

namespace cs {

struct AndroidAudio::Impl : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
    explicit Impl(audio::Synth& s) : synth(s) {}

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* data, int32_t frames) override {
        synth.render(static_cast<float*>(data), frames, stream->getChannelCount());
        if (tap) tap(tapCtx, static_cast<const float*>(data), frames, stream->getChannelCount());
        return oboe::DataCallbackResult::Continue;
    }
    void onErrorAfterClose(oboe::AudioStream*, oboe::Result error) override {
        // ErrorDisconnected: output device changed. Oboe allows reopening from this callback.
        CS_LOGW("Audio stream closed (%s); reopening", oboe::convertToText(error));
        std::lock_guard<std::mutex> lock(mutex);
        stream.reset();
        if (wanted) open();
    }

    bool open() {
        oboe::AudioStreamBuilder b;
        b.setDirection(oboe::Direction::Output)
            ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
            ->setSharingMode(oboe::SharingMode::Exclusive)
            ->setFormat(oboe::AudioFormat::Float)
            ->setChannelCount(oboe::ChannelCount::Stereo)
            ->setUsage(oboe::Usage::Game)
            ->setContentType(oboe::ContentType::Music)
            ->setDataCallback(this)
            ->setErrorCallback(this);
        const oboe::Result r = b.openStream(stream);
        if (r != oboe::Result::OK) { CS_LOGW("Audio unavailable: %s", oboe::convertToText(r)); stream.reset(); return false; }
        synth.setSampleRate(float(stream->getSampleRate())); // stream not started yet: no concurrent render
        stream->requestStart();
        CS_LOGI("Audio: %d Hz, %s, burst %d frames", stream->getSampleRate(),
                stream->getAudioApi() == oboe::AudioApi::AAudio ? "AAudio" : "OpenSL ES", stream->getFramesPerBurst());
        return true;
    }

    audio::Synth& synth;
    std::shared_ptr<oboe::AudioStream> stream;
    std::mutex mutex;
    bool wanted = false;
    Tap tap = nullptr;
    void* tapCtx = nullptr;
};

AndroidAudio::AndroidAudio(audio::Synth& synth) : impl_(std::make_unique<Impl>(synth)) {}
AndroidAudio::~AndroidAudio() { stop(); }

void AndroidAudio::start() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->wanted = true;
    if (!impl_->stream) impl_->open();
}

void AndroidAudio::setTap(Tap tap, void* ctx) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->tapCtx = ctx;
    impl_->tap = tap;
}

int AndroidAudio::sampleRate() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->stream ? impl_->stream->getSampleRate() : 48000;
}

void AndroidAudio::stop() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->wanted = false;
    if (impl_->stream) { impl_->stream->stop(); impl_->stream->close(); impl_->stream.reset(); }
}

} // namespace cs
