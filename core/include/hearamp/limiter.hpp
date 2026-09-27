// Limitador de saída (MPO — "maximum power output") sem look-ahead.
// Ataque instantâneo: nenhuma amostra excede o tecto, sem atraso adicional.
// Constitui a barreira de segurança auditiva do sistema e não é desactivável.
#pragma once

#include <algorithm>
#include <cmath>

namespace hearamp {

class PeakLimiter {
public:
    void setup(float fs, float ceilingDbFS, float releaseMs) {
        ceiling_ = std::pow(10.0f, std::min(ceilingDbFS, 0.0f) / 20.0f);
        release_ = std::exp(-1.0f / (0.001f * std::max(releaseMs, 1.0f) * fs));
    }

    float process(float x) {
        const float a = std::fabs(x);
        env_ = std::max(a, release_ * env_);
        const float g = env_ > ceiling_ ? ceiling_ / env_ : 1.0f;
        gainReduction_ = g;
        return std::clamp(x * g, -ceiling_, ceiling_);
    }

    void reset() { env_ = 0.0f; gainReduction_ = 1.0f; }
    float ceiling() const { return ceiling_; }
    float lastGain() const { return gainReduction_; }

private:
    float ceiling_ = 0.5f, release_ = 0.0f, env_ = 0.0f, gainReduction_ = 1.0f;
};

}  // namespace hearamp
