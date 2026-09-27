# Nota técnica — Amplificação de som ambiente em óculos inteligentes

**Objecto:** captar o som envolvente, amplificá-lo de forma selectiva e reproduzi-lo nos altifalantes das hastes dos óculos, em simultâneo com o som directo, sem atraso perceptível.

**Data de referência:** Setembro de 2026.

Convenção usada nesta nota:

- **[F]** facto verificado (medição própria neste repositório ou fonte citada);
- **[I]** interpretação técnica;
- **[R]** recomendação profissional.

---

## 1. A condicionante determinante: a latência

### 1.1 Porque é que "sem delay" é o requisito crítico

Os altifalantes das hastes são **abertos**: o som ambiente continua a chegar ao tímpano pelo ar, sem obstáculo. O utilizador ouve portanto **dois sinais sobrepostos**, o som directo e o som amplificado, que chega atrasado do tempo de processamento e transporte. Daqui resulta:

- **atraso inferior a ~10 ms:** o ouvido funde os dois sinais, mas a soma produz um **filtro em pente** (coloração metálica) [F];
- **limite de aceitabilidade em adaptações abertas:** perturbação classificada como "apenas aceitável" para atrasos de **~5–6 ms** (Stone et al., 2008, *Ear & Hearing*, "Tolerable hearing aid delays V") [F]. Os aparelhos auditivos comerciais operam tipicamente entre 5 e 8 ms, com um máximo de projecto de ~10 ms [F];
- **atraso acima de ~20–30 ms:** o som amplificado é percebido como **eco ou duplicação**, o que é incompatível com uma conversação confortável e interfere com a leitura labial (dessincronia audiovisual) [I].

### 1.2 Orçamento de latência por arquitectura

| Percurso | Latência típica total | Avaliação |
|---|---|---|
| DSP **no próprio óculo** (mic → ADC → DSP → DAC na haste) | **0,5–3 ms** | ✅ Imperceptível. É a única via verdadeiramente "sem delay" [I] |
| Telemóvel + ligação **com fios** (USB-C/jack) em modo AAudio MMAP | 10–25 ms (depende do aparelho) | ⚠️ Pente audível; eco ligeiro [I] |
| Telemóvel + **LE Audio (LC3)** | ~20–40 ms de transporte + 5–15 ms no telemóvel | ⚠️ Eco perceptível; tolerável sobretudo em perdas severas nos agudos [I] |
| Telemóvel + **Bluetooth clássico A2DP** (SBC/AAC) | 100–300 ms | ❌ Inviável para tempo real [I] |
| Telemóvel + Bluetooth **HFP/SCO** | elevada, banda estreita (voz) | ❌ Inviável [I] |

**Conclusão [I]:** uma app de telemóvel, por si só, **não consegue** cumprir "sem delay" com óculos ligados por Bluetooth. A latência dominante é a do transporte sem fios e não a do processamento. O requisito só é cumprido com **processamento local nos óculos**. A app fica então com o papel de consola de regulação (volume, programas, audiograma) e deixa de transportar o áudio.

### 1.3 Latência do processamento desenvolvido (medida)

O núcleo DSP deste repositório (`core/`) foi concebido para **latência algorítmica nula**. Todos os blocos são IIR causais aplicados amostra a amostra; não há FFT no percurso principal, look-ahead nem acumulação em blocos. Os valores seguintes foram medidos em `core/tests/test_main.cpp` [F]:

| Grandeza | Valor medido |
|---|---|
| Latência algorítmica | **0 amostras** |
| Pico da resposta impulsiva | amostra 3 (**0,06 ms**) |
| Atraso de grupo a 500 Hz / 1 kHz / 2 kHz / 4 kHz | 1,00 / 0,64 / 0,31 / 0,15 ms |
| Custo computacional (x86-64, 1 núcleo) | ~82× tempo real a 48 kHz |

O atraso de grupo residual resulta da fase mínima dos filtros de separação de bandas. Fica **abaixo de 1 ms** em toda a banda da fala, uma ordem de grandeza abaixo do limite de Stone et al. [I]

---

## 2. Cadeia de processamento

```
mic ─► passa-alto 150 Hz ─► passa-baixo 10 kHz ─► divisor LR4 3 bandas (800 Hz / 3 kHz)
        │                                              │        │        │
        │                                            WDRC     WDRC     WDRC
        │                                              └───┬────┴────────┘
        │                                                  ▼
        │                                  ganho mestre + volume (rampa 20 ms)
        │                                                  ▼
        │                               entalhes anti-Larsen (até 8, IIR) ◄──┐
        │                                                  ▼                 │ ramo lateral
        │                                  limitador MPO (sem look-ahead)    │ FFT 1024,
        │                                                  ▼                 │ salto 256
        └──────────────────────────────────────────► haste E + haste D ──────┘
```

| Bloco | Função | Justificação |
|---|---|---|
| Passa-alto 150 Hz (2.ª ordem) | Remove ruído de manuseamento, vento e graves | Os graves chegam directamente pela orelha aberta; amplificá-los só acrescenta mascaramento [I] |
| Passa-baixo 10 kHz | Limita a banda à resposta útil dos altifalantes de haste | Reduz o ruído de fundo e o risco de Larsen fora de banda [F: teste de laço fechado] |
| Divisor Linkwitz-Riley 4.ª ordem | Separa em graves, médios e agudos | A soma das bandas é plana (±0,05 dB medido) e não tem latência [F] |
| WDRC por banda | Ganho elevado para sons fracos, ganho reduzido para sons fortes; expansão em silêncio | Compensa o recrutamento (gama dinâmica residual reduzida) e atenua o ruído próprio dos microfones MEMS [I] |
| Supressor anti-Larsen | Detecta picos tonais persistentes e coloca entalhes estreitos; como último recurso reduz o ganho global | Microfone e altifalante ficam a poucos centímetros: o risco de apito é a principal limitação de ganho [I] |
| Limitador MPO | Tecto absoluto de saída, com ataque instantâneo | Barreira de segurança auditiva, não desactivável [R] |

Resultados de verificação [F]:

- **curva WDRC:** erro inferior a 0,25 dB face à curva estática teórica, de −80 a −20 dBFS;
- **tom estacionário de 3150 Hz:** atenuado 24 dB, com frequência estimada a ±1 Hz;
- **laço acústico instável simulado** (haste → microfone, 1,5 ms, ressonância a 2,5 kHz): o apito desce de −10,5 para −36 dBFS com ganho de percurso moderado, e de −6,6 para −55 dBFS com ganho de percurso elevado. Neste segundo caso actua o recuo de ganho global;
- **limitador:** nenhuma amostra excede o tecto, mesmo com +40 dB de ganho e transitórios de fundo de escala.

Limitações conhecidas [I]:

- Um **tom musical puro sustentado** acima de 400 Hz pode ser confundido com apito e entalhado temporariamente. O entalhe é estreito (~1/11 de oitava) e é libertado ao fim de 6 s. Para música, desligue o supressor ou use o programa "Televisão".
- A regra de ajuste por audiograma (`fitting.cpp`) é uma aproximação pública de meio-ganho e **não é** uma prescrição validada (NAL-NL2, DSL v5).

---

## 3. Vias de implementação

### Via A — Produto comercial certificado (uso pessoal imediato)

Existe no mercado europeu um produto que cumpre exactamente esta função: os **Nuance Audio** (EssilorLuxottica). São óculos com aparelho auditivo de condução aérea aberta integrado nas hastes, com microfones direccionais, **marcação CE ao abrigo do Regulamento (UE) 2017/745 (MDR)** e autorização da FDA como aparelho OTC. O lançamento europeu decorreu ao longo de 2025 [F].

**[R]** Se o objectivo é uso pessoal, esta é a solução tecnicamente adequada e juridicamente enquadrada. O processamento é local, pelo que a latência é própria de aparelho auditivo.

### Via B — Óculos comerciais com SDK (Ray-Ban Meta e similares)

O *Meta Wearables Device Access Toolkit* dá acesso ao conjunto de microfones e aos altifalantes abertos, mas o áudio é **encaminhado para a app no telemóvel**. A transmissão de PCM está marcada como experimental e indisponível para publicação em produção [F]. O percurso de ida e volta é Bluetooth, com latência de dezenas a centenas de ms.

**[I]** Esta via é **inviável para amplificação em tempo real**. Serve para funções diferidas, como transcrição e legendagem.

### Via C — App Android com óculos/auriculares LE Audio (protótipo funcional)

A pasta `android/` contém uma app completa: Kotlin, NDK e Oboe/AAudio em modo exclusivo de baixa latência, com o núcleo DSP deste repositório. Funcionalidades:

- selecção do microfone e da saída, com aviso automático quando a saída é A2DP/HFP;
- pré-selecção de dispositivos LE Audio;
- quatro programas, ganho por banda, volume, silenciamento, nível máximo de saída e supressão de Larsen;
- serviço em primeiro plano, para manter a amplificação com o ecrã desligado;
- reabertura automática dos fluxos se os óculos se desligarem;
- indicação da latência estimada pelo sistema.

**[I]** Serve para validar programas e regulações e para utilizadores com perda moderada a severa nos agudos, que toleram melhor o atraso. **Não** cumpre "sem delay" com ligação sem fios (ver 1.2).

### Via D — Processamento local nos óculos (a solução "sem delay")

O núcleo `core/` foi escrito para ser portado para um SoC embebido: C++17, sem alocação dinâmica no percurso de áudio, sem excepções nem dependências, e tamanho de FFT fixo em tempo de compilação. Arquitectura de referência [R]:

| Componente | Especificação indicativa |
|---|---|
| Microfones | 2 MEMS digitais (PDM) por haste, orientados para a frente, para direccionalidade futura (*beamforming*) |
| Processador | MCU com FPU (p. ex. Cortex-M33/M55) ou codec com núcleo DSP para *hearables*; LE Audio integrado se se pretender *streaming* |
| Conversores | Filtros de decimação/interpolação de baixa latência (fase mínima), para evitar os 1–2 ms dos filtros de fase linear por omissão |
| Taxa de amostragem | 24 kHz: banda útil de 10 kHz, suficiente para a fala, e metade do custo computacional |
| Transdutores | Altifalantes de haste com banda útil ≥ 8 kHz; afastamento e orientação escolhidos para maximizar o isolamento acústico ao microfone |
| App | Só controlo (BLE GATT: volume, programa, audiograma); o áudio nunca sai dos óculos |

**Custo computacional estimado [I]**, a confirmar por perfilagem no alvo, a 24 kHz:

- cerca de 25 biquads × ~10 ciclos por amostra;
- 3 WDRC × ~40 ciclos por amostra;
- FFT de 1024 pontos a cada 256 amostras, ~400 ciclos por amostra amortizados.

O total é da ordem de **20–25 MHz de um Cortex-M33 com FPU**, compatível com MCU de áudio de baixo consumo. A latência total resulta apenas dos conversores (tipicamente < 2 ms com filtros de fase mínima) e de um buffer de 8–16 amostras (< 1 ms).

---

## 4. Enquadramento regulamentar e de segurança (UE / Portugal)

| Tema | Enquadramento | Natureza |
|---|---|---|
| Dispositivo médico | Se a finalidade declarada for **compensar perda auditiva**, o equipamento é dispositivo médico ao abrigo do **Regulamento (UE) 2017/745 (MDR)**. Os aparelhos auditivos são, em regra, de **classe IIa**, com intervenção de organismo notificado. A autoridade competente em Portugal é o **INFARMED, I.P.** | [F] quanto ao regime; [I] quanto à classe, a confirmar caso a caso face à finalidade e às alegações |
| Amplificador pessoal (PSAP) | Na UE não existe categoria OTC equivalente à norte-americana. Um produto sem alegação médica ("amplificação de som ambiente") fica fora do MDR, mas qualquer alegação de compensação auditiva reconduz ao MDR | [I] — a validar juridicamente antes de comercialização |
| Rádio | Óculos com Bluetooth estão sujeitos à **Directiva 2014/53/UE (RED)** | [F] |
| Exposição sonora | Recomendações OMS/UIT (norma ITU-T H.870, "escuta segura"): referência de 80 dB(A) durante 40 h/semana para adultos. O MPO configurável e o limitador não desactivável deste projecto destinam-se a respeitar esse princípio; a calibração em dB SPL exige medição com simulador de ouvido no hardware final | [F] quanto à referência; [R] quanto à calibração |
| Protecção de dados | A captação contínua de áudio ambiente envolve terceiros. O processamento é efémero (nada é gravado nem transmitido), o que deve ser declarado na política de privacidade (RGPD) | [R] |

---

## 5. Riscos e pontos em aberto

1. **Latência de transporte sem fios** (via C). É irredutível pela app; a única mitigação é a via D. [I]
2. **Larsen com ganhos elevados.** Com altifalantes abertos a poucos centímetros dos microfones, o ganho estável máximo depende da geometria concreta da armação e só pode ser determinado por medição no protótipo. O limite de 30 dB por banda no código é conservador. [I]
3. **Calibração absoluta.** Os valores em dBFS só se convertem em dB SPL no tímpano com a sensibilidade dos microfones e a resposta dos altifalantes medidas. [R: medir antes de qualquer utilização prolongada]
4. **App Android não compilada neste ambiente.** O acesso ao SDK/NDK Android estava bloqueado. O código nativo foi verificado contra os cabeçalhos reais do Oboe 1.9.3; o Kotlin e o *build* Gradle requerem compilação no Android Studio. [F]
5. **Prescrição de ganho.** A regra incluída é indicativa. Para uso clínico é necessária adaptação por audiologista, com medição em ouvido real (REM). [R]

---

## Fontes

- Stone, M. A., Moore, B. C. J., Meisenbacher, K., Derleth, R. P. (2008). *Tolerable hearing aid delays. V. Estimation of limits for open canal fittings.* Ear & Hearing 29(4). https://pubmed.ncbi.nlm.nih.gov/18469715/
- Balling, L. W. et al. (2022). *Hearing aid delay in open-fit devices – coloration.* https://www.tandfonline.com/doi/full/10.1080/14992027.2022.2049380
- *Acceptable Processing Delay in Digital Hearing Aids*, The Hearing Review. https://hearingreview.com/practice-building/practice-management/acceptable-processing-delay-in-digital-hearing-aids
- EssilorLuxottica — FDA e certificação UE dos Nuance Audio. https://hearingreview.com/hearing-products/hearing-aids/otc/nuance-audio-glasses-get-fda-clearance-eu-certifications ; https://europe.ophthalmologytimes.com/view/nuance-audio-hearing-aid-glasses-receive-concurrent-ce-mark-and-fda-approval
- Meta Wearables Device Access Toolkit. https://developers.meta.com/blog/introducing-meta-wearables-device-access-toolkit/ ; https://github.com/facebook/meta-wearables-dat-ios
- R. Bristow-Johnson, *Audio EQ Cookbook* (coeficientes dos filtros biquadráticos).
- Regulamento (UE) 2017/745 (MDR); Directiva 2014/53/UE (RED); Recomendação ITU-T H.870.
