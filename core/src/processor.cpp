#include "hearamp/processor.hpp"

#include <cmath>

#if defined(__SSE__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace hearamp {

HearingAmplifier::HearingAmplifier(const Config& cfg) { configure(cfg); }

void HearingAmplifier::configure(const Config& cfg) {
    cfg_ = cfg;
    const float fs = cfg.sampleRate;
    const float nyq = 0.45f * fs;
    if (cfg.highpassHz > 0.0f)
        highpass_.set(BiquadCoeffs::highpass(fs, std::min(cfg.highpassHz, nyq), 0.70710678f));
    else
        highpass_.set(BiquadCoeffs::identity());
    if (cfg.lowpassHz > 0.0f && cfg.lowpassHz < nyq)
        lowpass_.set(BiquadCoeffs::lowpass(fs, cfg.lowpassHz, 0.70710678f));
    else
        lowpass_.set(BiquadCoeffs::identity());
    splitter_.setup(fs, std::min(cfg.crossoverLowHz, nyq), std::min(cfg.crossoverHighHz, nyq));
    for (std::size_t b = 0; b < wdrc_.size(); ++b) wdrc_[b].setup(fs, cfg.bands[b]);
    feedback_.setup(fs, cfg.feedback);
    limiter_.setup(fs, cfg.mpoDbFS, cfg.limiterReleaseMs);
    master_ = dbToLin(cfg.masterGainDb);
    smoothCoeff_ = std::exp(-1.0f / (0.020f * fs));  // 20 ms para volume/silenciamento
}

void HearingAmplifier::requestConfig(const Config& cfg) {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    pending_ = cfg;
    hasPending_.store(true, std::memory_order_release);
}

void HearingAmplifier::applyPending() {
    if (!hasPending_.load(std::memory_order_acquire)) return;
    std::unique_lock<std::mutex> lock(pendingMutex_, std::try_to_lock);
    if (!lock.owns_lock()) return;  // tenta de novo no próximo bloco
    configure(pending_);
    hasPending_.store(false, std::memory_order_relaxed);
}

void HearingAmplifier::reset() {
    highpass_.reset();
    lowpass_.reset();
    splitter_.reset();
    for (auto& w : wdrc_) w.reset();
    feedback_.reset();
    limiter_.reset();
}

float HearingAmplifier::processSample(float x) {
    float low, mid, high;
    splitter_.process(lowpass_.process(highpass_.process(x)), low, mid, high);
    float y = wdrc_[0].process(low) + wdrc_[1].process(mid) + wdrc_[2].process(high);
    y = feedback_.process(y * master_ * smoothedGain_);
    y = limiter_.process(y);
    feedback_.analyse(y);
    return y;
}

void HearingAmplifier::process(const float* in, float* out, std::size_t frames) {
    applyPending();
    const float target = muted_.load(std::memory_order_relaxed)
                             ? 0.0f
                             : dbToLin(volumeDb_.load(std::memory_order_relaxed));
    for (std::size_t i = 0; i < frames; ++i) {
        smoothedGain_ = target + smoothCoeff_ * (smoothedGain_ - target);
        if (target == 0.0f && smoothedGain_ < 1e-5f) smoothedGain_ = 0.0f;
        out[i] = processSample(in[i]);
    }
}

ScopedFlushDenormals::ScopedFlushDenormals() {
#if defined(__SSE__) || defined(_M_X64)
    saved_ = _mm_getcsr();
    _mm_setcsr(static_cast<unsigned>(saved_) | 0x8040);  // FTZ | DAZ
#elif defined(__aarch64__)
    unsigned long long fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    saved_ = fpcr;
    asm volatile("msr fpcr, %0" ::"r"(fpcr | (1ULL << 24)));  // FZ
#endif
}

ScopedFlushDenormals::~ScopedFlushDenormals() {
#if defined(__SSE__) || defined(_M_X64)
    _mm_setcsr(static_cast<unsigned>(saved_));
#elif defined(__aarch64__)
    asm volatile("msr fpcr, %0" ::"r"(saved_));
#endif
}

}  // namespace hearamp
