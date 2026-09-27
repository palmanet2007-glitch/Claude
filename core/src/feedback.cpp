#include "hearamp/feedback.hpp"

#include <algorithm>
#include <cmath>

namespace hearamp {

FeedbackSuppressor::FeedbackSuppressor() {
    for (std::size_t i = 0; i < kFftSize; ++i)
        window_[i] = 0.5f - 0.5f * std::cos(2.0f * kPi * static_cast<float>(i) / kFftSize);
    for (std::size_t k = 0; k < kFftSize / 2; ++k)
        twiddle_[k] = std::polar(1.0f, -2.0f * kPi * static_cast<float>(k) / kFftSize);
}

void FeedbackSuppressor::setup(float fs, const FeedbackConfig& cfg) {
    fs_ = fs;
    cfg_ = cfg;
    for (auto& n : notches_)
        if (n.active) updateNotchFilter(n);
}

void FeedbackSuppressor::reset() {
    ring_.fill(0.0f);
    for (auto& n : notches_) { n.active = false; n.filter.reset(); }
    for (auto& c : candidates_) c = {};
    writePos_ = hopCounter_ = 0;
    backoffDb_ = backoffIdle_ = 0.0f;
    backoffGain_ = 1.0f;
    backoffStep_ = 0.0f;
}

int FeedbackSuppressor::activeNotches() const {
    return static_cast<int>(std::count_if(notches_.begin(), notches_.end(),
                                          [](const Notch& n) { return n.active; }));
}

bool FeedbackSuppressor::notchInfo(int i, float& hz, float& depthDb) const {
    if (i < 0 || i >= kMaxNotches || !notches_[i].active) return false;
    hz = notches_[i].hz;
    depthDb = notches_[i].depthDb;
    return true;
}

void FeedbackSuppressor::fft(std::array<std::complex<float>, kFftSize>& a) const {
    // Radix-2 iterativo, in-place, sem alocação (seguro em tempo real).
    for (std::size_t i = 1, j = 0; i < kFftSize; ++i) {
        std::size_t bit = kFftSize >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= kFftSize; len <<= 1) {
        const std::size_t stride = kFftSize / len;
        for (std::size_t i = 0; i < kFftSize; i += len)
            for (std::size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * twiddle_[k * stride];
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
    }
}

void FeedbackSuppressor::runAnalysis() {
    const float frameSeconds = static_cast<float>(kHop) / fs_;

    // Libertação progressiva dos entalhes sem nova detecção.
    for (auto& n : notches_) {
        if (!n.active) continue;
        n.idleSeconds += frameSeconds;
        if (n.idleSeconds > cfg_.holdSeconds) {
            n.depthDb += cfg_.releaseDbPerSecond * frameSeconds;
            if (n.depthDb >= -0.5f) { n.active = false; n.filter.reset(); }
            else updateNotchFilter(n);
        }
    }
    if (!cfg_.enabled) {
        backoffDb_ = 0.0f;
        backoffStep_ = (1.0f - backoffGain_) / static_cast<float>(kHop);
        return;
    }

    for (std::size_t i = 0; i < kFftSize; ++i)
        work_[i] = {ring_[(writePos_ + i) & (kFftSize - 1)] * window_[i], 0.0f};
    fft(work_);

    // Normalização: janela de Hann tem ganho coerente 0.5 -> sinusóide de amplitude 1 = 0 dBFS.
    const float norm = 2.0f / (0.5f * kFftSize);
    const std::size_t half = kFftSize / 2;
    for (std::size_t k = 0; k <= half; ++k)
        magDb_[k] = 20.0f * std::log10(std::abs(work_[k]) * norm + 1e-12f);

    const float binHz = fs_ / kFftSize;
    const std::size_t kMin = std::max<std::size_t>(4, static_cast<std::size_t>(cfg_.minHz / binHz));
    const std::size_t kMax = std::min(half - 4, static_cast<std::size_t>(cfg_.maxHz / binHz));
    if (kMin >= kMax) return;

    // Mediana espectral na gama de análise: estimador do fundo robusto à presença
    // do próprio apito (a média seria dominada por ele).
    const std::size_t count = kMax - kMin + 1;
    std::copy(magDb_.begin() + kMin, magDb_.begin() + kMax + 1, scratch_.begin());
    std::nth_element(scratch_.begin(), scratch_.begin() + count / 2, scratch_.begin() + count);
    const float floorDb = scratch_[count / 2];

    // Recolhe os picos tonais que cumprem os critérios (vários modos de oscilação
    // podem coexistir), por ordem decrescente de saliência.
    int numPeaks = 0;
    for (std::size_t k = kMin; k <= kMax; ++k) {
        const float m = magDb_[k];
        if (m < cfg_.minLevelDbFS || m < magDb_[k - 1] || m < magDb_[k + 1]) continue;
        if (m - floorDb < cfg_.peakToMedianDb) continue;
        const float neighbours = std::max({magDb_[k - 4], magDb_[k - 3], magDb_[k + 3], magDb_[k + 4]});
        if (m - neighbours < cfg_.peakToNeighbourDb) continue;
        // Interpolação parabólica para estimativa sub-bin da frequência.
        const float a = magDb_[k - 1], c = magDb_[k + 1];
        const float den = a - 2.0f * m + c;
        const float delta = std::fabs(den) > 1e-9f ? 0.5f * (a - c) / den : 0.0f;
        const Peak p{(static_cast<float>(k) + delta) * binHz, m - floorDb};
        if (numPeaks < kMaxPeaks) {
            peaks_[numPeaks++] = p;
        } else {
            auto weakest = std::min_element(peaks_.begin(), peaks_.end(),
                                            [](const Peak& x, const Peak& y) { return x.prominence < y.prominence; });
            if (p.prominence > weakest->prominence) *weakest = p;
        }
    }

    // Persistência: só um pico estável durante N tramas é tratado como apito
    // (a fala e a música variam em frequência; o Larsen não).
    std::array<bool, kMaxPeaks> used{};
    for (auto& c : candidates_) {
        if (c.count == 0) continue;
        bool matched = false;
        for (int i = 0; i < numPeaks; ++i) {
            if (!used[i] && std::fabs(c.hz - peaks_[i].hz) < matchTolerance(c.hz)) {
                used[i] = matched = true;
                c.hz = 0.5f * (c.hz + peaks_[i].hz);
                if (++c.count >= cfg_.persistenceFrames) {
                    engage(c.hz);
                    c = {};
                }
                break;
            }
        }
        if (!matched) --c.count;  // decaimento lento: tolera tramas mascaradas por fala
    }
    for (int i = 0; i < numPeaks; ++i) {
        if (used[i]) continue;
        auto slot = std::min_element(candidates_.begin(), candidates_.end(),
                                     [](const Candidate& a, const Candidate& b) { return a.count < b.count; });
        if (slot->count <= 1) *slot = {peaks_[i].hz, 1};
    }

    // Recuperação lenta do recuo de ganho global.
    backoffIdle_ += frameSeconds;
    if (backoffDb_ < 0.0f && backoffIdle_ > cfg_.holdSeconds)
        backoffDb_ = std::min(0.0f, backoffDb_ + cfg_.backoffRecoveryDbPerSecond * frameSeconds);
    // Rampa até ao novo ganho ao longo do próximo salto (sem cliques).
    const float target = std::pow(10.0f, backoffDb_ / 20.0f);
    backoffStep_ = (target - backoffGain_) / static_cast<float>(kHop);
}

float FeedbackSuppressor::matchTolerance(float hz) const {
    // O apito tende a migrar para o flanco de um entalhe já colocado: considera-se
    // o mesmo modo dentro da largura de banda do entalhe (f/Q) ou de 1,5 bins.
    return std::max(1.5f * fs_ / kFftSize, hz / cfg_.notchQ);
}

void FeedbackSuppressor::engage(float hz) {
    auto backoff = [this] {
        backoffDb_ = std::max(backoffDb_ + cfg_.backoffStepDb, cfg_.maxBackoffDb);
        backoffIdle_ = 0.0f;
    };
    for (auto& n : notches_) {
        if (n.active && std::fabs(n.hz - hz) < matchTolerance(n.hz)) {
            if (n.depthDb <= cfg_.maxDepthDb) backoff();
            n.depthDb = std::max(n.depthDb + cfg_.depthStepDb, cfg_.maxDepthDb);
            // Recentra o entalhe na frequência de oscilação actual.
            n.hz = 0.5f * (n.hz + hz);
            n.idleSeconds = 0.0f;
            updateNotchFilter(n);
            return;
        }
    }
    Notch* slot = nullptr;
    for (auto& n : notches_)
        if (!n.active) { slot = &n; break; }
    if (!slot) {  // todos ocupados: recua o ganho e reutiliza o entalhe mais inactivo
        backoff();
        slot = &*std::max_element(notches_.begin(), notches_.end(),
                                  [](const Notch& a, const Notch& b) { return a.idleSeconds < b.idleSeconds; });
    }
    slot->active = true;
    slot->hz = hz;
    slot->depthDb = cfg_.initialDepthDb;
    slot->idleSeconds = 0.0f;
    slot->filter.reset();
    updateNotchFilter(*slot);
}

void FeedbackSuppressor::updateNotchFilter(Notch& n) {
    n.filter.set(BiquadCoeffs::peaking(fs_, n.hz, cfg_.notchQ, n.depthDb));
}

}  // namespace hearamp
