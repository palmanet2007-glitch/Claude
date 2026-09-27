// Pré-regulações e ajuste indicativo a partir de audiograma.
//
// AVISO: a regra aqui implementada é uma aproximação pública de meio-ganho
// (Lybarger) com compressão progressiva; NÃO substitui prescrições validadas
// (NAL-NL2, DSL v5) nem a adaptação por audiologista com medição em ouvido real.
#pragma once

#include <array>

#include "hearamp/processor.hpp"

namespace hearamp {

// Limiares auditivos (dB HL) a 250, 500, 1000, 2000, 4000 e 8000 Hz.
struct Audiogram {
    static constexpr std::array<float, 6> kFrequencies{250, 500, 1000, 2000, 4000, 8000};
    std::array<float, 6> thresholdsDbHL{};
};

enum class Preset {
    Conversation,  // realce da inteligibilidade da fala (1–4 kHz)
    Restaurant,    // ruído de fundo: menos graves, compressão mais rápida
    Television,    // gama alargada, compressão suave
    Outdoor,       // corte de vento reforçado
};

Config makePreset(Preset preset, float sampleRate);

// Ganho máximo recomendado por banda para altifalantes de haste (condução aérea
// aberta): acima disto o risco de Larsen e distorção torna-se dominante.
constexpr float kMaxOpenEarGainDb = 30.0f;

Config fitFromAudiogram(const Audiogram& a, const Config& base);

}  // namespace hearamp
