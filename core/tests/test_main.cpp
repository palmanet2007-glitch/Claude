// Testes de verificação do núcleo DSP (sem dependências externas).
#include <cmath>
#include <chrono>
#include <complex>
#include <cstdio>
#include <random>
#include <vector>

#include "hearamp/fitting.hpp"
#include "hearamp/processor.hpp"

using namespace hearamp;

static int g_failures = 0;
#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (!(cond)) {                                                \
            ++g_failures;                                             \
            std::printf("  FALHA %s:%d: %s — ", __FILE__, __LINE__, #cond); \
            std::printf(__VA_ARGS__);                                 \
            std::printf("\n");                                        \
        }                                                             \
    } while (0)

constexpr float kFs = 48000.0f;

static double rms(const std::vector<float>& v, std::size_t from, std::size_t to) {
    double s = 0;
    for (std::size_t i = from; i < to; ++i) s += double(v[i]) * v[i];
    return std::sqrt(s / double(to - from));
}

static std::vector<float> sine(float hz, float amp, std::size_t n) {
    std::vector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = amp * std::sin(2.0f * kPi * hz * i / kFs);
    return v;
}

// Configuração linear e transparente (0 dB, sem compressão/expansão).
static Config flatConfig() {
    Config c;
    c.sampleRate = kFs;
    c.highpassHz = 0.0f;
    c.lowpassHz = 0.0f;
    for (auto& b : c.bands) b = BandConfig{};
    c.mpoDbFS = 0.0f;
    c.feedback.enabled = false;
    return c;
}

static void testCrossoverFlat() {
    std::printf("[divisor LR4] soma das bandas plana em amplitude\n");
    for (float hz : {100.f, 400.f, 800.f, 1500.f, 3000.f, 6000.f, 12000.f}) {
        ThreeBandSplitter s;
        s.setup(kFs, 800, 3000);
        auto x = sine(hz, 0.5f, 24000);
        std::vector<float> y(x.size());
        for (std::size_t i = 0; i < x.size(); ++i) {
            float l, m, h;
            s.process(x[i], l, m, h);
            y[i] = l + m + h;
        }
        const double db = 20 * std::log10(rms(y, 12000, 24000) / rms(x, 12000, 24000));
        CHECK(std::fabs(db) < 0.05, "%.0f Hz: %.3f dB", hz, db);
    }
}

// Atraso de grupo a partir da resposta impulsiva: tau = Re{DFT(n·h)/DFT(h)}.
static double groupDelayMs(const std::vector<float>& h, float hz) {
    std::complex<double> H = 0, nH = 0;
    const double w = 2 * M_PI * hz / kFs;
    for (std::size_t n = 0; n < h.size(); ++n) {
        const auto e = std::polar(1.0, -w * double(n));
        H += double(h[n]) * e;
        nH += double(n) * double(h[n]) * e;
    }
    return (nH / H).real() / kFs * 1000.0;
}

static void testLatency() {
    std::printf("[latência] resposta impulsiva da cadeia completa\n");
    Config c = flatConfig();
    c.highpassHz = 150.0f;  // configuração realista de serviço
    c.lowpassHz = 10000.0f;
    HearingAmplifier amp(c);
    std::vector<float> h(16384, 0.0f), out(h.size());
    h[0] = 0.5f;
    amp.process(h.data(), out.data(), h.size());
    std::size_t peak = 0;
    for (std::size_t i = 1; i < out.size(); ++i)
        if (std::fabs(out[i]) > std::fabs(out[peak])) peak = i;
    std::printf("  pico da resposta impulsiva na amostra %zu (%.3f ms)\n", peak, peak / kFs * 1000);
    CHECK(HearingAmplifier::kAlgorithmicLatencySamples == 0, "latência algorítmica declarada");
    CHECK(peak <= 12, "pico em %zu (> 0,25 ms)", peak);
    for (float hz : {500.f, 1000.f, 2000.f, 4000.f, 8000.f}) {
        const double gd = groupDelayMs(out, hz);
        std::printf("  atraso de grupo a %5.0f Hz: %.3f ms\n", hz, gd);
        CHECK(gd < (hz < 1000.f ? 1.2 : 1.0), "%.0f Hz: %.3f ms", hz, gd);
    }
}

static void testWdrcStaticCurve() {
    std::printf("[WDRC] curva estática entrada/saída\n");
    BandConfig b;
    b.gainDb = 20;
    b.kneeDbFS = -40;
    b.ratio = 3;
    b.expansionKneeDbFS = -70;
    b.expansionRatio = 2;
    Wdrc w;
    w.setup(kFs, b);
    for (float inDb : {-80.f, -60.f, -50.f, -40.f, -30.f, -20.f}) {
        w.reset();
        auto x = sine(1000, dbToLin(inDb), 48000);
        std::vector<float> y(x.size());
        for (std::size_t i = 0; i < x.size(); ++i) y[i] = w.process(x[i]);
        const double g = 20 * std::log10(rms(y, 24000, 48000) / rms(x, 24000, 48000));
        const double expected = w.staticGainDb(inDb);
        std::printf("  entrada %5.0f dBFS -> ganho %6.2f dB (esperado %6.2f)\n", inDb, g, expected);
        CHECK(std::fabs(g - expected) < 0.5, "%.0f dBFS: %.2f vs %.2f", inDb, g, expected);
    }
}

static void testLimiterCeiling() {
    std::printf("[MPO] nenhuma amostra excede o tecto, mesmo com ganho excessivo\n");
    Config c = flatConfig();
    c.mpoDbFS = -12.0f;
    for (auto& b : c.bands) b.gainDb = 40.0f;
    HearingAmplifier amp(c);
    std::mt19937 rng(1);
    std::normal_distribution<float> nd(0.0f, 0.3f);
    std::vector<float> x(96000), y(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = nd(rng) + (i % 9600 == 0 ? 1.0f : 0.0f);
    amp.process(x.data(), y.data(), x.size());
    float mx = 0;
    for (float v : y) mx = std::max(mx, std::fabs(v));
    const float ceiling = dbToLin(-12.0f);
    std::printf("  máximo |saída| = %.5f, tecto = %.5f\n", mx, ceiling);
    CHECK(mx <= ceiling + 1e-6f, "%.6f > %.6f", mx, ceiling);
}

static void testVolumeAndMute() {
    std::printf("[controlo] volume e silenciamento suavizados\n");
    HearingAmplifier amp(flatConfig());
    auto x = sine(1000, 0.1f, 9600);
    std::vector<float> y(x.size());
    amp.setVolumeDb(-6.0f);
    amp.process(x.data(), y.data(), x.size());
    const double g = 20 * std::log10(rms(y, 4800, 9600) / rms(x, 4800, 9600));
    CHECK(std::fabs(g + 6.0) < 0.1, "volume: %.2f dB", g);
    amp.setMuted(true);
    amp.process(x.data(), y.data(), x.size());
    CHECK(rms(y, 8600, 9600) < 1e-5, "silenciamento: %.2e", rms(y, 8600, 9600));
    // A transição de silenciamento não deve ter descontinuidades (clique).
    float maxStep = 0;
    for (std::size_t i = 1; i < 4800; ++i) maxStep = std::max(maxStep, std::fabs(y[i] - y[i - 1]));
    CHECK(maxStep < 0.02f, "salto máximo %.4f", maxStep);
}

static void testFeedbackToneRemoval() {
    std::printf("[anti-Larsen] tom estacionário é entalhado; ruído de banda larga preservado\n");
    Config c = flatConfig();
    c.feedback.enabled = true;
    HearingAmplifier amp(c);
    std::mt19937 rng(7);
    std::normal_distribution<float> nd(0.0f, 0.01f);
    const std::size_t n = 48000;
    std::vector<float> noise(n), x(n), y(n);
    for (std::size_t i = 0; i < n; ++i) {
        noise[i] = nd(rng);
        x[i] = noise[i] + 0.3f * std::sin(2.0f * kPi * 3150.0f * i / kFs);
    }
    amp.process(x.data(), y.data(), n);
    // Nível do tom à saída (correlação com a sinusóide) no último quarto de segundo.
    auto toneLevel = [&](const std::vector<float>& v) {
        std::complex<double> acc = 0;
        for (std::size_t i = n - 12000; i < n; ++i)
            acc += double(v[i]) * std::polar(1.0, -2 * M_PI * 3150.0 * i / kFs);
        return 20 * std::log10(2 * std::abs(acc) / 12000.0);
    };
    const double inTone = toneLevel(x), outTone = toneLevel(y);
    float hz = 0, depth = 0;
    amp.feedbackSuppressor().notchInfo(0, hz, depth);
    std::printf("  tom: entrada %.1f dB, saída %.1f dB; entalhes activos %d (%.0f Hz, %.0f dB)\n", inTone,
                outTone, amp.activeFeedbackNotches(), hz, depth);
    CHECK(amp.activeFeedbackNotches() >= 1, "nenhum entalhe");
    CHECK(inTone - outTone > 20.0, "atenuação só de %.1f dB", inTone - outTone);
    CHECK(std::fabs(hz - 3150.0f) < 30.0f, "frequência estimada %.1f", hz);
}

// Nível (dBFS) do pico espectral mais forte entre 300 Hz e 10 kHz (janela de Hann).
static double strongestPeakDb(const std::vector<float>& y, float& atHz) {
    const std::size_t len = 8192, from = y.size() - len;
    double best = -200;
    for (float f = 300; f <= 10000; f += 5) {
        std::complex<double> a = 0;
        for (std::size_t j = 0; j < len; ++j) {
            const double w = 0.5 - 0.5 * std::cos(2 * M_PI * j / len);
            a += w * y[from + j] * std::polar(1.0, -2 * M_PI * f * j / kFs);
        }
        const double d = 20 * std::log10(4 * std::abs(a) / len + 1e-12);
        if (d > best) { best = d; atHz = f; }
    }
    return best;
}

static void testFeedbackClosedLoop() {
    std::printf("[anti-Larsen] laço acústico fechado instável (haste -> microfone)\n");
    // Percurso acústico simulado: atraso de 1,5 ms (conversores + propagação) com
    // ressonância a 2,5 kHz; o ganho do percurso torna o laço instável nessa zona.
    auto run = [](float pathGain, bool suppress, int& notches, float& peakHz) {
        Config c = makePreset(Preset::Conversation, kFs);
        c.feedback.enabled = suppress;
        HearingAmplifier amp(c);
        Biquad path;
        path.set(BiquadCoeffs::peaking(kFs, 2500.0f, 4.0f, 12.0f));
        const std::size_t delay = 72, n = 6 * 48000;
        std::vector<float> ring(delay, 0.0f), y(n);
        std::mt19937 rng(3);
        std::normal_distribution<float> nd(0.0f, 0.003f);  // ambiente calmo
        for (std::size_t i = 0; i < n; ++i) {
            const float fb = pathGain * path.process(ring[i % delay]);
            y[i] = amp.processSample(nd(rng) + fb);
            ring[i % delay] = y[i];
        }
        notches = amp.activeFeedbackNotches();
        return strongestPeakDb(y, peakHz);
    };
    for (float g : {0.12f, 0.35f}) {
        int nOff = 0, nOn = 0;
        float fOff = 0, fOn = 0;
        const double off = run(g, false, nOff, fOff), on = run(g, true, nOn, fOn);
        std::printf("  ganho de percurso %.2f: pico tonal sem supressor %.1f dBFS @ %.0f Hz; "
                    "com supressor %.1f dBFS @ %.0f Hz (%d entalhes)\n", g, off, fOff, on, fOn, nOn);
        CHECK(off > -15.0, "o cenário deveria oscilar sem supressor (%.1f dBFS)", off);
        CHECK(off - on > 25.0, "redução do apito de apenas %.1f dB", off - on);
    }
}

static void testFitting() {
    std::printf("[ajuste] audiograma -> ganhos por banda\n");
    Audiogram mild{{15, 20, 30, 40, 50, 55}};  // presbiacusia ligeira a moderada típica
    const Config c = fitFromAudiogram(mild, makePreset(Preset::Conversation, kFs));
    for (int b = 0; b < 3; ++b)
        std::printf("  banda %d: ganho %.1f dB, razão %.2f\n", b, c.bands[b].gainDb, c.bands[b].ratio);
    CHECK(c.bands[0].gainDb < c.bands[1].gainDb && c.bands[1].gainDb < c.bands[2].gainDb, "ordem");
    Audiogram severe{{90, 90, 90, 90, 90, 90}};
    const Config s = fitFromAudiogram(severe, makePreset(Preset::Conversation, kFs));
    for (auto& b : s.bands) CHECK(b.gainDb <= kMaxOpenEarGainDb, "ganho %.1f", b.gainDb);
}

static void testStability() {
    std::printf("[robustez] silêncio prolongado e entradas extremas sem NaN/Inf\n");
    ScopedFlushDenormals ftz;
    HearingAmplifier amp(makePreset(Preset::Restaurant, kFs));
    std::vector<float> x(48000, 0.0f), y(x.size());
    x[10] = 1.0f;
    x[20] = -1.0f;
    for (int k = 0; k < 10; ++k) amp.process(x.data(), y.data(), x.size());
    bool finite = true;
    for (float v : y) finite &= std::isfinite(v);
    CHECK(finite, "valor não finito");
}

static void testThroughput() {
    std::printf("[desempenho] custo computacional\n");
    HearingAmplifier amp(makePreset(Preset::Conversation, kFs));
    std::vector<float> x = sine(1000, 0.1f, 480000), y(x.size());
    const auto t0 = std::chrono::steady_clock::now();
    amp.process(x.data(), y.data(), x.size());
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  10 s de áudio a 48 kHz processados em %.1f ms (%.0fx tempo real)\n", s * 1000, 10.0 / s);
}

int main() {
    testCrossoverFlat();
    testLatency();
    testWdrcStaticCurve();
    testLimiterCeiling();
    testVolumeAndMute();
    testFeedbackToneRemoval();
    testFeedbackClosedLoop();
    testFitting();
    testStability();
    testThroughput();
    std::printf(g_failures ? "\n%d FALHA(S)\n" : "\nTodos os testes passaram.\n", g_failures);
    return g_failures ? 1 : 0;
}
