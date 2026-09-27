// Compressão de gama dinâmica ampla (WDRC) por banda, com expansão descendente
// para atenuar o ruído próprio dos microfones em silêncio.
// O detector de nível e o cálculo de ganho actuam sobre a amostra corrente:
// o ganho é aplicado sem look-ahead, pelo que a latência algorítmica é nula.
#pragma once

#include <algorithm>
#include <cmath>

namespace hearamp {

struct BandConfig {
    float gainDb = 0.0f;                  // ganho de inserção abaixo do joelho
    float kneeDbFS = -40.0f;              // limiar de compressão (nível de entrada)
    float ratio = 1.0f;                   // razão de compressão (1 = linear)
    float expansionKneeDbFS = -90.0f;     // abaixo deste nível aplica-se expansão
    float expansionRatio = 1.0f;          // 1 = sem expansão
    float attackMs = 5.0f;
    float releaseMs = 80.0f;
};

inline float dbToLin(float db) { return std::pow(10.0f, db / 20.0f); }
inline float linToDb(float lin) { return 20.0f * std::log10(std::max(lin, 1e-9f)); }

class Wdrc {
public:
    void setup(float fs, const BandConfig& cfg) {
        cfg_ = cfg;
        cfg_.ratio = std::max(cfg_.ratio, 1.0f);
        cfg_.expansionRatio = std::max(cfg_.expansionRatio, 1.0f);
        attack_ = std::exp(-1.0f / (0.001f * std::max(cfg.attackMs, 0.05f) * fs));
        release_ = std::exp(-1.0f / (0.001f * std::max(cfg.releaseMs, 1.0f) * fs));
        smooth_ = std::exp(-1.0f / (0.001f * fs));
        // Recalcula o ganho no próximo bloco de controlo.
        counter_ = 0;
    }

    // Ganho estático (dB) para um nível de entrada estacionário (dBFS).
    float staticGainDb(float levelDb) const {
        float g = cfg_.gainDb;
        if (levelDb > cfg_.kneeDbFS)
            g -= (levelDb - cfg_.kneeDbFS) * (1.0f - 1.0f / cfg_.ratio);
        if (levelDb < cfg_.expansionKneeDbFS)
            g -= std::min((cfg_.expansionKneeDbFS - levelDb) * (cfg_.expansionRatio - 1.0f), kMaxExpansionDb);
        return g;
    }

    float process(float x) {
        // Detector quadrático: pré-alisamento fixo (1 ms) remove a ondulação a 2f
        // do quadrado do sinal; seguem-se as constantes de ataque/libertação.
        power_ = smooth_ * power_ + (1.0f - smooth_) * x * x;
        const float p = power_;
        env_ = p > env_ ? attack_ * env_ + (1.0f - attack_) * p
                        : release_ * env_ + (1.0f - release_) * p;
        if (counter_-- <= 0) {
            // Ganho calculado à cadência de controlo e interpolado linearmente
            // entre blocos, para reduzir o custo de pow/log por amostra.
            counter_ = kControlBlock - 1;
            // Convenção AES17: uma sinusóide de amplitude 1 corresponde a 0 dBFS.
            const float levelDb = 10.0f * std::log10(std::max(env_, 1e-18f)) + kAes17OffsetDb;
            target_ = dbToLin(staticGainDb(levelDb));
            step_ = (target_ - gain_) / static_cast<float>(kControlBlock);
        }
        gain_ += step_;
        return x * gain_;
    }

    void reset() { power_ = env_ = 0.0f; gain_ = target_ = 1.0f; step_ = 0.0f; counter_ = 0; }
    float currentGainDb() const { return linToDb(gain_); }

private:
    static constexpr int kControlBlock = 16;
    static constexpr float kMaxExpansionDb = 30.0f;
    static constexpr float kAes17OffsetDb = 3.0103f;  // 10·log10(2)

    BandConfig cfg_;
    float attack_ = 0.0f, release_ = 0.0f, smooth_ = 0.0f, power_ = 0.0f;
    float env_ = 0.0f, gain_ = 1.0f, target_ = 1.0f, step_ = 0.0f;
    int counter_ = 0;
};

}  // namespace hearamp
