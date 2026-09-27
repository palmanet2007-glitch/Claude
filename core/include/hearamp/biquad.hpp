// Filtros biquadráticos (forma directa II transposta) com coeficientes segundo
// o "Audio EQ Cookbook" (R. Bristow-Johnson). Todos os filtros são IIR causais:
// não introduzem latência algorítmica (sem look-ahead nem blocos de análise).
#pragma once

#include <cmath>

namespace hearamp {

constexpr float kPi = 3.14159265358979323846f;

struct BiquadCoeffs {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;

    static BiquadCoeffs identity() { return {}; }

    static BiquadCoeffs lowpass(float fs, float f0, float q) {
        const float w0 = 2.0f * kPi * f0 / fs, c = std::cos(w0), al = std::sin(w0) / (2.0f * q);
        return normalise((1.0f - c) * 0.5f, 1.0f - c, (1.0f - c) * 0.5f, 1.0f + al, -2.0f * c, 1.0f - al);
    }

    static BiquadCoeffs highpass(float fs, float f0, float q) {
        const float w0 = 2.0f * kPi * f0 / fs, c = std::cos(w0), al = std::sin(w0) / (2.0f * q);
        return normalise((1.0f + c) * 0.5f, -(1.0f + c), (1.0f + c) * 0.5f, 1.0f + al, -2.0f * c, 1.0f - al);
    }

    static BiquadCoeffs allpass(float fs, float f0, float q) {
        const float w0 = 2.0f * kPi * f0 / fs, c = std::cos(w0), al = std::sin(w0) / (2.0f * q);
        return normalise(1.0f - al, -2.0f * c, 1.0f + al, 1.0f + al, -2.0f * c, 1.0f - al);
    }

    // Filtro de pico (campânula); gainDb < 0 produz um entalhe de profundidade finita.
    static BiquadCoeffs peaking(float fs, float f0, float q, float gainDb) {
        const float A = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * f0 / fs, c = std::cos(w0), al = std::sin(w0) / (2.0f * q);
        return normalise(1.0f + al * A, -2.0f * c, 1.0f - al * A, 1.0f + al / A, -2.0f * c, 1.0f - al / A);
    }

    static BiquadCoeffs highShelf(float fs, float f0, float gainDb) {
        const float A = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * f0 / fs, c = std::cos(w0);
        const float al = std::sin(w0) / 2.0f * std::sqrt(2.0f);  // declive S = 1
        const float sa = 2.0f * std::sqrt(A) * al;
        return normalise(A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c),
                         A * ((A + 1) + (A - 1) * c - sa), (A + 1) - (A - 1) * c + sa,
                         2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
    }

private:
    static BiquadCoeffs normalise(float b0, float b1, float b2, float a0, float a1, float a2) {
        const float k = 1.0f / a0;
        return {b0 * k, b1 * k, b2 * k, a1 * k, a2 * k};
    }
};

class Biquad {
public:
    void set(const BiquadCoeffs& c) { c_ = c; }
    void reset() { z1_ = z2_ = 0.0f; }

    float process(float x) {
        const float y = c_.b0 * x + z1_;
        z1_ = c_.b1 * x - c_.a1 * y + z2_;
        z2_ = c_.b2 * x - c_.a2 * y;
        return y;
    }

private:
    BiquadCoeffs c_;
    float z1_ = 0.0f, z2_ = 0.0f;
};

}  // namespace hearamp
