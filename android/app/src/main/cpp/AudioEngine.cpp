#include "AudioEngine.h"

#include <android/log.h>

#include <algorithm>
#include <thread>

#define LOG_TAG "HearAmp"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {
constexpr int32_t kMaxFrames = 4096;
}

AudioEngine& AudioEngine::instance() {
    static AudioEngine engine;
    return engine;
}

bool AudioEngine::start(int inputDeviceId, int outputDeviceId) {
    std::lock_guard<std::mutex> lock(streamLock_);
    if (running_) closeStreams();
    inputDeviceId_ = inputDeviceId;
    outputDeviceId_ = outputDeviceId;
    if (!openStreams()) {
        closeStreams();
        return false;
    }
    running_ = true;
    return true;
}

void AudioEngine::stop() {
    std::lock_guard<std::mutex> lock(streamLock_);
    closeStreams();
    running_ = false;
}

bool AudioEngine::openStreams() {
    // 1) Saída: define a taxa de amostragem nativa e o ritmo dos callbacks.
    oboe::AudioStreamBuilder out;
    out.setDirection(oboe::Direction::Output)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive)
        ->setFormat(oboe::AudioFormat::Float)
        ->setChannelCount(oboe::ChannelCount::Stereo)  // haste esquerda + haste direita
        ->setUsage(oboe::Usage::Media)
        ->setContentType(oboe::ContentType::Speech)
        ->setDeviceId(outputDeviceId_)
        ->setDataCallback(this)
        ->setErrorCallback(this);
    oboe::Result r = out.openStream(output_);
    if (r != oboe::Result::OK) {
        LOGE("Falha ao abrir saída: %s", oboe::convertToText(r));
        return false;
    }

    // 2) Entrada à mesma taxa; preset VoicePerformance = percurso sem AGC/NS de baixa latência.
    oboe::AudioStreamBuilder in;
    in.setDirection(oboe::Direction::Input)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive)
        ->setFormat(oboe::AudioFormat::Float)
        ->setChannelCount(oboe::ChannelCount::Mono)
        ->setSampleRate(output_->getSampleRate())
        ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
        ->setInputPreset(oboe::InputPreset::VoicePerformance)
        ->setDeviceId(inputDeviceId_);
    r = in.openStream(input_);
    if (r != oboe::Result::OK) {
        LOGE("Falha ao abrir entrada: %s", oboe::convertToText(r));
        return false;
    }

    // Buffer de saída mínimo estável: 2 bursts (o ajuste automático de underruns pode aumentá-lo).
    output_->setBufferSizeInFrames(output_->getFramesPerBurst() * 2);

    inChannels_ = input_->getChannelCount();
    outChannels_ = output_->getChannelCount();
    inBuffer_.assign(static_cast<size_t>(kMaxFrames) * inChannels_, 0.0f);
    monoBuffer_.assign(kMaxFrames, 0.0f);
    drainInput_ = true;
    underruns_ = 0;

    const int fs = output_->getSampleRate();
    sampleRate_ = fs;
    {
        std::lock_guard<std::mutex> lock(configLock_);
        uiConfig_.sampleRate = static_cast<float>(fs);
        amp_.configure(uiConfig_);  // fluxos ainda parados: configuração directa é segura
        amp_.reset();
    }

    LOGI("Entrada: dispositivo %d, %d Hz, burst %d | Saída: dispositivo %d, %d Hz, burst %d, %s",
         input_->getDeviceId(), input_->getSampleRate(), input_->getFramesPerBurst(), output_->getDeviceId(),
         fs, output_->getFramesPerBurst(),
         output_->getSharingMode() == oboe::SharingMode::Exclusive ? "exclusivo" : "partilhado");

    if (input_->requestStart() != oboe::Result::OK || output_->requestStart() != oboe::Result::OK) {
        LOGE("Falha ao iniciar os fluxos");
        return false;
    }
    return true;
}

void AudioEngine::closeStreams() {
    if (output_) { output_->stop(); output_->close(); output_.reset(); }
    if (input_) { input_->stop(); input_->close(); input_.reset(); }
}

oboe::DataCallbackResult AudioEngine::onAudioReady(oboe::AudioStream*, void* audioData, int32_t numFrames) {
    hearamp::ScopedFlushDenormals ftz;
    auto* out = static_cast<float*>(audioData);
    const int32_t frames = std::min(numFrames, kMaxFrames);
    // Os fluxos só são fechados depois de a saída parar, logo sem callbacks em curso.
    oboe::AudioStream* input = input_.get();

    if (drainInput_ && input) {
        // Descarta o que se acumulou durante o arranque: latência mínima desde o início.
        while (true) {
            auto res = input->read(inBuffer_.data(), kMaxFrames, 0);
            if (!res || res.value() <= 0) break;
        }
        drainInput_ = false;
    }

    int32_t got = 0;
    if (input) {
        // Se a entrada acumulou mais do que 2 callbacks (deriva de relógios), descarta o excesso.
        auto avail = input->getAvailableFrames();
        if (avail && avail.value() > 2 * frames) {
            int32_t excess = avail.value() - frames;
            while (excess > 0) {
                auto res = input->read(inBuffer_.data(), std::min(excess, kMaxFrames), 0);
                if (!res || res.value() <= 0) break;
                excess -= res.value();
            }
        }
        auto res = input->read(inBuffer_.data(), frames, 0);
        if (res) got = res.value();
    }
    if (got < frames) ++underruns_;

    for (int32_t i = 0; i < frames; ++i)
        monoBuffer_[i] = i < got ? inBuffer_[static_cast<size_t>(i) * inChannels_] : 0.0f;

    amp_.process(monoBuffer_.data(), monoBuffer_.data(), static_cast<size_t>(frames));

    for (int32_t i = 0; i < numFrames; ++i) {
        const float v = i < frames ? monoBuffer_[i] : 0.0f;
        for (int ch = 0; ch < outChannels_; ++ch) out[i * outChannels_ + ch] = v;
    }
    return oboe::DataCallbackResult::Continue;
}

void AudioEngine::onErrorAfterClose(oboe::AudioStream*, oboe::Result error) {
    // Tipicamente ErrorDisconnected: os óculos desligaram-se ou mudou o encaminhamento.
    LOGE("Fluxo fechado (%s); a reabrir", oboe::convertToText(error));
    if (!running_) return;
    std::thread([this] {
        std::lock_guard<std::mutex> lock(streamLock_);
        if (!running_) return;
        closeStreams();
        // Após desconexão, volta ao encaminhamento automático.
        inputDeviceId_ = outputDeviceId_ = 0;
        running_ = openStreams();
        if (!running_) closeStreams();
    }).detach();
}

double AudioEngine::estimatedLatencyMs() {
    std::lock_guard<std::mutex> lock(streamLock_);
    if (!input_ || !output_) return -1.0;
    auto li = input_->calculateLatencyMillis();
    auto lo = output_->calculateLatencyMillis();
    if (!li || !lo) return -1.0;
    return li.value() + lo.value();
}

void AudioEngine::pushConfig() { amp_.requestConfig(uiConfig_); }

void AudioEngine::setPreset(hearamp::Preset preset) {
    std::lock_guard<std::mutex> lock(configLock_);
    const auto mpo = uiConfig_.mpoDbFS;
    const auto fb = uiConfig_.feedback.enabled;
    uiConfig_ = hearamp::makePreset(preset, static_cast<float>(sampleRate_.load()));
    uiConfig_.mpoDbFS = mpo;
    uiConfig_.feedback.enabled = fb;
    pushConfig();
}

void AudioEngine::setBandGains(float lowDb, float midDb, float highDb) {
    std::lock_guard<std::mutex> lock(configLock_);
    uiConfig_.bands[0].gainDb = std::clamp(lowDb, -10.0f, hearamp::kMaxOpenEarGainDb);
    uiConfig_.bands[1].gainDb = std::clamp(midDb, -10.0f, hearamp::kMaxOpenEarGainDb);
    uiConfig_.bands[2].gainDb = std::clamp(highDb, -10.0f, hearamp::kMaxOpenEarGainDb);
    pushConfig();
}

void AudioEngine::setMpoDbFS(float db) {
    std::lock_guard<std::mutex> lock(configLock_);
    uiConfig_.mpoDbFS = std::clamp(db, -30.0f, 0.0f);
    pushConfig();
}

void AudioEngine::setFeedbackEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(configLock_);
    uiConfig_.feedback.enabled = enabled;
    pushConfig();
}

void AudioEngine::applyAudiogram(const hearamp::Audiogram& a) {
    std::lock_guard<std::mutex> lock(configLock_);
    uiConfig_ = hearamp::fitFromAudiogram(a, uiConfig_);
    pushConfig();
}
