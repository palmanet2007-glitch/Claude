// Divisor de três bandas Linkwitz-Riley de 4.ª ordem (LR4).
// A soma das três bandas é um passa-tudo: resposta em amplitude plana, sem
// latência algorítmica. O atraso de grupo residual é inferior a 1 ms acima
// de ~500 Hz (verificado em tests/test_main.cpp).
#pragma once

#include "hearamp/biquad.hpp"

namespace hearamp {

class LR4 {
public:
    void setLowpass(float fs, float fc) {
        const auto c = BiquadCoeffs::lowpass(fs, fc, kButterworthQ);
        a_.set(c);
        b_.set(c);
    }
    void setHighpass(float fs, float fc) {
        const auto c = BiquadCoeffs::highpass(fs, fc, kButterworthQ);
        a_.set(c);
        b_.set(c);
    }
    float process(float x) { return b_.process(a_.process(x)); }
    void reset() { a_.reset(); b_.reset(); }

private:
    static constexpr float kButterworthQ = 0.70710678f;
    Biquad a_, b_;
};

class ThreeBandSplitter {
public:
    void setup(float fs, float fcLow, float fcHigh) {
        lp1_.setLowpass(fs, fcLow);
        hp1_.setHighpass(fs, fcLow);
        lp2_.setLowpass(fs, fcHigh);
        hp2_.setHighpass(fs, fcHigh);
        // LP_LR4 + HP_LR4 = passa-tudo de 2.ª ordem com Q = 1/sqrt(2); aplicado à
        // banda grave, alinha a sua fase com a soma média+aguda.
        apLow_.set(BiquadCoeffs::allpass(fs, fcHigh, 0.70710678f));
    }

    void process(float x, float& low, float& mid, float& high) {
        const float h = hp1_.process(x);
        low = apLow_.process(lp1_.process(x));
        mid = lp2_.process(h);
        high = hp2_.process(h);
    }

    void reset() {
        lp1_.reset(); hp1_.reset(); lp2_.reset(); hp2_.reset(); apLow_.reset();
    }

private:
    LR4 lp1_, hp1_, lp2_, hp2_;
    Biquad apLow_;
};

}  // namespace hearamp
