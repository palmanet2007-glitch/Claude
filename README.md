# Amplificação de som ambiente para óculos inteligentes

Sistema de amplificação do som envolvente reproduzido nas hastes de óculos inteligentes, em simultâneo com o som directo. O núcleo de processamento tem **latência algorítmica nula**.

> **Leia primeiro [`docs/ARQUITECTURA.md`](docs/ARQUITECTURA.md).** Com altifalantes abertos, o som amplificado soma-se ao som directo; para não haver eco, o atraso total tem de ficar abaixo de ~5–10 ms. O processamento aqui desenvolvido cumpre largamente esse limite (< 1 ms). Uma ligação **Bluetooth** entre telemóvel e óculos não o cumpre: A2DP fica em 100–300 ms e LE Audio em ~20–40 ms. Uma experiência verdadeiramente "sem delay" exige correr o núcleo DSP **nos próprios óculos** (via D da nota técnica).

## Conteúdo

| Pasta | Descrição |
|---|---|
| `core/` | Biblioteca DSP portátil em C++17, sem alocação no percurso de áudio: filtros, divisor LR4 de 3 bandas, compressão WDRC, supressão de Larsen e limitador de saída (MPO). Serve a app Android e a portagem para SoC embebido |
| `core/tests/` | Testes de verificação: latência, planura do divisor, curva WDRC, tecto do limitador, anti-Larsen em laço fechado, robustez e desempenho |
| `core/tools/hearamp_wav.cpp` | Ferramenta de bancada para processar ficheiros WAV com os programas |
| `android/` | App Android (Kotlin + NDK + Oboe/AAudio) com encaminhamento de dispositivos, programas, regulações e serviço em primeiro plano |
| `docs/ARQUITECTURA.md` | Nota técnica: orçamento de latência, vias de implementação, enquadramento regulamentar e riscos |

## Compilar e testar o núcleo

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/hearamp_tests
./build/hearamp_wav entrada.wav saida.wav conversa     # conversa | restaurante | tv | exterior
```

## Compilar a app Android

Abra a pasta `android/` no Android Studio (Ladybug ou posterior), com SDK 35 e NDK 27. O projecto usa o AGP 8.7 e obtém o Oboe 1.9.3 via Maven (prefab). A app requer Android 10 (API 29) ou superior; o LE Audio requer Android 13 e telemóvel compatível.

> O código nativo foi verificado contra os cabeçalhos do Oboe 1.9.3. O projecto Gradle/Kotlin ainda não foi compilado, porque o ambiente de desenvolvimento não tinha acesso ao SDK Android.

## Segurança

O limitador de saída não pode ser desactivado. O ganho por banda está limitado a 30 dB. Este software **não é um dispositivo médico certificado** e não substitui um aparelho auditivo prescrito e adaptado por um profissional.
