// Supressor de realimentação acústica (efeito Larsen) por entalhes adaptativos.
//
// Nos óculos, o altifalante da haste está a poucos centímetros dos microfones:
// com ganhos elevados o laço acústico entra em oscilação (apito). A detecção é
// feita num ramo lateral (FFT sobre o sinal de saída já emitido); a correcção
// é aplicada no percurso principal por filtros IIR de entalhe estreitos, pelo
// que o supressor não acrescenta latência ao som amplificado.
#pragma once

#include <array>
#include <complex>
#include <cstddef>

#include "hearamp/biquad.hpp"

namespace hearamp {

struct FeedbackConfig {
    bool enabled = true;
    float minHz = 400.0f;             // gama típica de apito em aparelhos abertos
    float maxHz = 11000.0f;
    float peakToMedianDb = 30.0f;     // saliência mínima do pico face à mediana espectral
    float peakToNeighbourDb = 12.0f;  // saliência face aos bins vizinhos (tonalidade)
    float minLevelDbFS = -60.0f;      // abaixo disto não há risco de apito audível
    int persistenceFrames = 10;       // ~53 ms a 48 kHz (salto de 256 amostras)
    float notchQ = 16.0f;             // ~1/11 de oitava
    float initialDepthDb = -12.0f;
    float depthStepDb = -6.0f;
    float maxDepthDb = -36.0f;
    float holdSeconds = 6.0f;         // sem nova detecção, o entalhe começa a libertar
    float releaseDbPerSecond = 3.0f;
    // Último recurso: se os entalhes se esgotam ou um entalhe no máximo volta a
    // disparar, reduz-se o ganho global (e recupera-se lentamente).
    float backoffStepDb = -3.0f;
    float maxBackoffDb = -12.0f;
    float backoffRecoveryDbPerSecond = 1.0f;
};

class FeedbackSuppressor {
public:
    static constexpr int kMaxNotches = 8;
    static constexpr std::size_t kFftSize = 1024;
    static constexpr std::size_t kHop = 256;

    FeedbackSuppressor();

    void setup(float fs, const FeedbackConfig& cfg);
    void reset();

    // Percurso principal: aplica os entalhes activos (IIR, latência nula).
    float process(float x) {
        for (int i = 0; i < kMaxNotches; ++i)
            if (notches_[i].active) x = notches_[i].filter.process(x);
        backoffGain_ += backoffStep_;
        return x * backoffGain_;
    }

    // Ramo lateral: alimentar com a amostra efectivamente emitida.
    void analyse(float y) {
        ring_[writePos_] = y;
        writePos_ = (writePos_ + 1) & (kFftSize - 1);
        if (++hopCounter_ >= kHop) {
            hopCounter_ = 0;
            runAnalysis();
        }
    }

    int activeNotches() const;
    float backoffDb() const { return backoffDb_; }
    // Frequência (Hz) e profundidade (dB) de cada entalhe activo, para diagnóstico.
    bool notchInfo(int i, float& hz, float& depthDb) const;

private:
    struct Notch {
        bool active = false;
        float hz = 0.0f, depthDb = 0.0f;
        float idleSeconds = 0.0f;
        Biquad filter;
    };
    struct Candidate {
        float hz = 0.0f;
        int count = 0;
    };
    struct Peak {
        float hz = 0.0f, prominence = 0.0f;
    };
    static constexpr int kMaxPeaks = kMaxNotches;

    void runAnalysis();
    void fft(std::array<std::complex<float>, kFftSize>& a) const;
    void engage(float hz);
    float matchTolerance(float hz) const;
    void updateNotchFilter(Notch& n);

    float fs_ = 48000.0f;
    FeedbackConfig cfg_;
    std::array<float, kFftSize> ring_{};
    std::array<float, kFftSize> window_{};
    std::array<std::complex<float>, kFftSize / 2> twiddle_{};
    std::array<std::complex<float>, kFftSize> work_{};
    std::array<float, kFftSize / 2 + 1> magDb_{};
    std::array<float, kFftSize / 2 + 1> scratch_{};
    std::array<Notch, kMaxNotches> notches_{};
    std::array<Candidate, kMaxNotches> candidates_{};
    std::array<Peak, kMaxPeaks> peaks_{};
    float backoffDb_ = 0.0f, backoffIdle_ = 0.0f;
    float backoffGain_ = 1.0f, backoffStep_ = 0.0f;
    std::size_t writePos_ = 0, hopCounter_ = 0;
};

}  // namespace hearamp
