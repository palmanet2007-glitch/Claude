// Ferramenta de bancada: processa um ficheiro WAV com a cadeia de amplificação,
// para avaliação auditiva/objectiva offline das regulações.
//
//   hearamp_wav entrada.wav saida.wav [conversa|restaurante|tv|exterior] [volume_dB]
//
// Suporta PCM 16 bits e float 32 bits, mono ou estéreo (usa o canal esquerdo).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "hearamp/fitting.hpp"
#include "hearamp/processor.hpp"

namespace {

struct Wav {
    uint32_t sampleRate = 48000;
    uint16_t channels = 1;
    std::vector<float> mono;
};

template <typename T>
T readLE(std::ifstream& f) {
    T v{};
    f.read(reinterpret_cast<char*>(&v), sizeof(T));
    return v;
}

bool readWav(const char* path, Wav& w) {
    std::ifstream f(path, std::ios::binary);
    char id[4];
    f.read(id, 4);
    if (!f || std::memcmp(id, "RIFF", 4) != 0) return false;
    readLE<uint32_t>(f);
    f.read(id, 4);
    if (std::memcmp(id, "WAVE", 4) != 0) return false;
    uint16_t format = 0, bits = 0;
    while (f.read(id, 4)) {
        const uint32_t size = readLE<uint32_t>(f);
        if (std::memcmp(id, "fmt ", 4) == 0) {
            format = readLE<uint16_t>(f);
            w.channels = readLE<uint16_t>(f);
            w.sampleRate = readLE<uint32_t>(f);
            readLE<uint32_t>(f);
            readLE<uint16_t>(f);
            bits = readLE<uint16_t>(f);
            f.seekg(size - 16, std::ios::cur);
            if (format == 0xFFFE) format = bits == 32 ? 3 : 1;  // WAVE_FORMAT_EXTENSIBLE
        } else if (std::memcmp(id, "data", 4) == 0) {
            const std::size_t frames = size / (bits / 8) / w.channels;
            w.mono.resize(frames);
            for (std::size_t i = 0; i < frames; ++i) {
                for (uint16_t ch = 0; ch < w.channels; ++ch) {
                    float v;
                    if (format == 3 && bits == 32) v = readLE<float>(f);
                    else if (format == 1 && bits == 16) v = readLE<int16_t>(f) / 32768.0f;
                    else return false;
                    if (ch == 0) w.mono[i] = v;
                }
            }
            return true;
        } else {
            f.seekg(size + (size & 1), std::ios::cur);
        }
    }
    return false;
}

void writeWav(const char* path, const std::vector<float>& x, uint32_t fs) {
    std::ofstream f(path, std::ios::binary);
    auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<char*>(&v), 4); };
    auto w16 = [&](uint16_t v) { f.write(reinterpret_cast<char*>(&v), 2); };
    const uint32_t dataBytes = static_cast<uint32_t>(x.size() * 4);
    f.write("RIFF", 4); w32(36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); w32(16); w16(3); w16(1); w32(fs); w32(fs * 4); w16(4); w16(32);
    f.write("data", 4); w32(dataBytes);
    f.write(reinterpret_cast<const char*>(x.data()), dataBytes);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "uso: %s entrada.wav saida.wav [conversa|restaurante|tv|exterior] [volume_dB]\n",
                     argv[0]);
        return 2;
    }
    Wav in;
    if (!readWav(argv[1], in)) {
        std::fprintf(stderr, "não foi possível ler %s (PCM16 ou float32 requerido)\n", argv[1]);
        return 1;
    }
    const std::string p = argc > 3 ? argv[3] : "conversa";
    const hearamp::Preset preset = p == "restaurante" ? hearamp::Preset::Restaurant
                                 : p == "tv"          ? hearamp::Preset::Television
                                 : p == "exterior"    ? hearamp::Preset::Outdoor
                                                      : hearamp::Preset::Conversation;
    hearamp::HearingAmplifier amp(hearamp::makePreset(preset, static_cast<float>(in.sampleRate)));
    if (argc > 4) amp.setVolumeDb(std::stof(argv[4]));
    std::vector<float> out(in.mono.size());
    {
        hearamp::ScopedFlushDenormals ftz;
        amp.process(in.mono.data(), out.data(), out.size());
    }
    writeWav(argv[2], out, in.sampleRate);
    std::printf("%zu amostras a %u Hz processadas (predefinição '%s'); entalhes anti-Larsen activos: %d\n",
                out.size(), in.sampleRate, p.c_str(), amp.activeFeedbackNotches());
    return 0;
}
