#include "hearamp/fitting.hpp"

#include <algorithm>

namespace hearamp {

namespace {

BandConfig band(float gainDb, float kneeDbFS, float ratio, float attackMs, float releaseMs) {
    BandConfig b;
    b.gainDb = gainDb;
    b.kneeDbFS = kneeDbFS;
    b.ratio = ratio;
    b.expansionKneeDbFS = -65.0f;  // silêncio: atenua ruído dos MEMS
    b.expansionRatio = 2.0f;
    b.attackMs = attackMs;
    b.releaseMs = releaseMs;
    return b;
}

}  // namespace

Config makePreset(Preset preset, float sampleRate) {
    Config c;
    c.sampleRate = sampleRate;
    switch (preset) {
        case Preset::Conversation:
            c.highpassHz = 150.0f;
            c.bands = {band(4, -35, 1.5f, 5, 80), band(14, -40, 2.0f, 5, 80), band(18, -40, 2.2f, 3, 60)};
            break;
        case Preset::Restaurant:
            c.highpassHz = 250.0f;
            c.bands = {band(-2, -40, 2.0f, 3, 50), band(12, -38, 2.5f, 3, 50), band(15, -38, 2.5f, 2, 40)};
            break;
        case Preset::Television:
            c.highpassHz = 100.0f;
            c.bands = {band(6, -30, 1.3f, 10, 150), band(10, -32, 1.5f, 10, 150), band(12, -32, 1.6f, 8, 120)};
            break;
        case Preset::Outdoor:
            c.highpassHz = 300.0f;
            c.bands = {band(0, -35, 2.0f, 3, 60), band(12, -38, 2.0f, 5, 80), band(14, -38, 2.2f, 3, 60)};
            break;
    }
    return c;
}

Config fitFromAudiogram(const Audiogram& a, const Config& base) {
    const auto& t = a.thresholdsDbHL;
    // Perda média por banda (dB HL): grave 250–500, média 1000–2000, aguda 2000–8000 (ponderada a 4 kHz).
    const std::array<float, 3> loss{
        0.5f * (t[0] + t[1]),
        0.5f * (t[2] + t[3]),
        0.25f * t[3] + 0.5f * t[4] + 0.25f * t[5],
    };
    // Correcções de banda: os graves recebem menos ganho (mascaramento ascendente,
    // som directo pela orelha aberta e efeito de oclusão inexistente/irrelevante).
    const std::array<float, 3> correction{-8.0f, 0.0f, -2.0f};

    Config c = base;
    for (std::size_t b = 0; b < 3; ++b) {
        const float l = std::max(loss[b], 0.0f);
        auto& bc = c.bands[b];
        bc.gainDb = std::clamp(0.5f * l + correction[b], 0.0f, kMaxOpenEarGainDb);
        // Recrutamento: mais perda -> gama dinâmica residual menor -> mais compressão.
        bc.ratio = std::clamp(1.0f + l / 50.0f, 1.0f, 3.0f);
        bc.kneeDbFS = -45.0f;
        bc.expansionKneeDbFS = -65.0f;
        bc.expansionRatio = 2.0f;
    }
    return c;
}

}  // namespace hearamp
