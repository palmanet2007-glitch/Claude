// Motor de áudio full-duplex de baixa latência (Oboe/AAudio).
//
// O fluxo de saída comanda o relógio: em cada callback lê-se, sem bloqueio, o
// mesmo número de tramas do fluxo de entrada, processa-se e escreve-se. A
// latência adicionada pela app limita-se aos buffers dos dois fluxos (tipicamente
// 2 × burst); o DSP não acrescenta latência algorítmica.
#pragma once

#include <oboe/Oboe.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "hearamp/fitting.hpp"
#include "hearamp/processor.hpp"

class AudioEngine : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
public:
    static AudioEngine& instance();

    // deviceId = 0 -> encaminhamento automático do sistema.
    bool start(int inputDeviceId, int outputDeviceId);
    void stop();
    bool isRunning() const { return running_.load(); }

    // Parâmetros (thread da UI). São encaminhados para o DSP sem bloquear o áudio.
    void setPreset(hearamp::Preset preset);
    void setBandGains(float lowDb, float midDb, float highDb);
    void setMpoDbFS(float db);
    void setFeedbackEnabled(bool enabled);
    void applyAudiogram(const hearamp::Audiogram& a);
    void setVolumeDb(float db) { amp_.setVolumeDb(db); }
    void setMuted(bool muted) { amp_.setMuted(muted); }

    // Diagnóstico.
    double estimatedLatencyMs();
    int sampleRate() const { return sampleRate_.load(); }
    int activeNotches() const { return amp_.activeFeedbackNotches(); }
    int underruns() const { return underruns_.load(); }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* audioData, int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;

private:
    AudioEngine() = default;
    bool openStreams();
    void closeStreams();
    void pushConfig();

    std::mutex streamLock_;
    std::shared_ptr<oboe::AudioStream> input_, output_;
    int inputDeviceId_ = 0, outputDeviceId_ = 0;
    std::atomic<bool> running_{false};
    std::atomic<int> sampleRate_{48000};
    std::atomic<int> underruns_{0};

    std::vector<float> inBuffer_, monoBuffer_;
    int inChannels_ = 1, outChannels_ = 2;
    bool drainInput_ = true;

    hearamp::HearingAmplifier amp_;
    std::mutex configLock_;
    hearamp::Config uiConfig_ = hearamp::makePreset(hearamp::Preset::Conversation, 48000.0f);
};
