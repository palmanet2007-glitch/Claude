// Cadeia completa de amplificação de som ambiente para óculos inteligentes.
//
//   microfone -> passa-alto/passa-baixo -> divisor 3 bandas LR4 -> WDRC por banda -> soma
//             -> ganho mestre -> entalhes anti-Larsen -> limitador MPO -> haste
//
// Todos os blocos são IIR/amostra-a-amostra: latência algorítmica = 0 amostras.
// A latência total percebida é, portanto, a do transporte de áudio (conversores,
// buffers do sistema operativo e, sobretudo, a ligação sem fios) — ver docs/.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>

#include "hearamp/biquad.hpp"
#include "hearamp/crossover.hpp"
#include "hearamp/feedback.hpp"
#include "hearamp/limiter.hpp"
#include "hearamp/wdrc.hpp"

namespace hearamp {

struct Config {
    float sampleRate = 48000.0f;
    float highpassHz = 150.0f;         // remove ruído de manuseamento/vento e reduz efeito de oclusão
    float lowpassHz = 10000.0f;        // largura de banda útil de altifalantes de haste; limita ruído e Larsen fora de banda
    float crossoverLowHz = 800.0f;
    float crossoverHighHz = 3000.0f;
    std::array<BandConfig, 3> bands{};  // grave, média, aguda
    float masterGainDb = 0.0f;
    float mpoDbFS = -6.0f;             // tecto absoluto de saída
    float limiterReleaseMs = 60.0f;
    FeedbackConfig feedback{};
};

class HearingAmplifier {
public:
    static constexpr int kAlgorithmicLatencySamples = 0;

    explicit HearingAmplifier(const Config& cfg = Config{});

    // Reconfiguração imediata. Não aloca memória, mas não é segura entre threads:
    // chamar apenas a partir da thread de áudio ou com o fluxo parado.
    void configure(const Config& cfg);

    // Reconfiguração a partir de qualquer thread (UI). É aplicada no início do
    // próximo bloco de áudio; a thread de áudio nunca bloqueia (try_lock).
    void requestConfig(const Config& cfg);

    // Ajustes contínuos seguros entre threads (com suavização anti-clique).
    void setVolumeDb(float db) { volumeDb_.store(db, std::memory_order_relaxed); }
    void setMuted(bool m) { muted_.store(m, std::memory_order_relaxed); }

    // Processa um bloco mono. in e out podem coincidir (processamento in-place).
    void process(const float* in, float* out, std::size_t frames);
    float processSample(float x);

    void reset();

    const Config& config() const { return cfg_; }
    int activeFeedbackNotches() const { return feedback_.activeNotches(); }
    const FeedbackSuppressor& feedbackSuppressor() const { return feedback_; }
    float limiterGain() const { return limiter_.lastGain(); }

private:
    void applyPending();

    Config cfg_;
    Biquad highpass_, lowpass_;
    ThreeBandSplitter splitter_;
    std::array<Wdrc, 3> wdrc_;
    FeedbackSuppressor feedback_;
    PeakLimiter limiter_;
    float master_ = 1.0f;
    float smoothedGain_ = 1.0f, smoothCoeff_ = 0.0f;

    std::atomic<float> volumeDb_{0.0f};
    std::atomic<bool> muted_{false};

    std::mutex pendingMutex_;
    Config pending_;
    std::atomic<bool> hasPending_{false};
};

// Protecção contra desnormais (evita picos de CPU em silêncio) em x86 e ARM.
class ScopedFlushDenormals {
public:
    ScopedFlushDenormals();
    ~ScopedFlushDenormals();
    ScopedFlushDenormals(const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator=(const ScopedFlushDenormals&) = delete;

private:
    unsigned long long saved_ = 0;
};

}  // namespace hearamp
