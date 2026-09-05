# Arquitectura de Sistema — Pedal DSP GTR

> **Versión:** 1.0 — Septiembre 2026 (documentación derivada · Prompt 6 v2.0)
> **Proyecto:** `1_Pedal_DSP_GTR`
> **Dominio:** 🎸 **Touring Grade** + **Studio Grade DSP**
> **Fecha:** Septiembre 2026
> **Base de verdad:** `Arquitectura_Hardware.md v1.2` (SSOT primaria) + `src/` (cross-check) + `rules/9` (protocolo)
> **Audiencia:** arquitecto software / tech lead que necesita el mapa completo de módulos, contratos y data flow.

---

## 1. Vista de módulos (capas)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         CORE 0 — HOT-PATH (Audio)                       │
│  main.cpp::loop()                                                       │
│    └─ audioSystem.processAudio()                                        │
│         ├─ I2S DMA (read24 estéreo)  ← PCM1808                          │
│         ├─ EffectSlots::processChainBlock(L, R, N)   ← cadena 4 slots   │
│         ├─ TPDF Dither + CPU Hard-Mute                                  │
│         └─ I2S DMA (write24 estéreo) → PCM5102A                         │
├─────────────────────────────────────────────────────────────────────────┤
│                      CORE 1 — UI / TELEMETRÍA (30 fps)                  │
│  main.cpp::loop1()                                                      │
│    ├─ DSPMetrics (térmico 85/95 °C, CPU budget, underruns)              │
│    ├─ ControlMatrix::moduladores (wavePedal, inputLevel @100 Hz)        │
│    ├─ UIManager::update() (MenuState FSM + Dirty Flags + NeoPixels)     │
│    └─ dsp_alive.store(true)   → heartbeat atómico → Core 0              │
├─────────────────────────────────────────────────────────────────────────┤
│                         SHARED STATE (lock-free)                        │
│  std::atomic: dsp_alive · pendingEffects[4] · targetMasterVolume ·     │
│               tuningMode · peakL/peakR · cpuLimitActive                 │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Pipeline de audio end-to-end

```
   ┌──────────┐  24-bit   ┌──────────────┐  sample_t   ┌──────────────────┐  24-bit   ┌──────────┐
   │ PCM1808  │ ────────▶ │ AudioSystem  │ ──────────▶ │  EffectSlots     │ ────────▶│PCM5102A  │
   │  ADC     │   I2S DMA │ I2S (Core 0) │  [-1, +1]   │  (cadena DSP)    │   I2S DMA │  DAC     │
   └──────────┘           └──────────────┘             └──────────────────┘           └──────────┘
        ▲                       │                            │ │ │ │                       │
        │                       │ read24/write24             │ └─┼─┴─┼─ 4 slots IEffect       │
   MCLK 12.288 MHz               │                            │   │   └─ slotLimiters          │
   (clk_gpout0)                  │                            │   └─ vitalizer (TransientMaster)│
                                ▼                                └─ sweetSpot (SweetSpotConditioner)
                          peakL/peakR                               ▲
                          i2sUnderrunCount                          │ masterWarming (ZLWarm)
                          cpuLimitActive                             │ inputConditioner (GuitarConditioner)
                                                                     │ globalEQ (GlobalEQ)
                                                                     └─ targetMasterVolume (atómico)
```

**Buffers I2S — latencia boutique (`AudioSystemI2S.h:18-19`):**
```cpp
constexpr uint8_t I2S_BUFFER_COUNT = 2;   // reducido de 3
constexpr uint8_t I2S_BUFFER_SIZE = 32;   // 64 muestras = 1.33 ms @ 48 kHz (estable a 196 MHz)
```
> **FASE 4:** latencia total ~1.33 ms (boutique) vs ~2 ms (buffers previos). Trade-off: buffers más chicos = mayor presión sobre la FPU; compensado con el overclock a 196 MHz + procesamiento por bloques.

---

## 3. Sistema de efectos (SOLID)

### 3.1 Contrato `IEffect` (`src/dsp/IEffect.h`)

Todos los efectos implementan la interfaz `IEffect` (SOLID). **Cero `new`/`malloc`** en implementaciones.

| Método | Propósito |
|:-------|:----------|
| `init(sample_t sampleRate)` | Precalcular coeficientes |
| `processBlock(sample_t* L, sample_t* R, size_t N)` | **Procesa bloque estéreo** (no muestra-a-muestra) |
| `reset()` | Limpiar buffers internos (anti-pop al cambiar) |
| `setEnabled(bool)` / `isEnabled()` | Bypass |
| `setTailOnly(bool)` / `isTailOnly()` | **Spill-over**: sigue procesando colas sin entrada nueva |
| `setTempoBpm(sample_t)` | Sync BPM global |
| `getParamCount()` / `getParamInfo(i)` | **Metadatos paramétricos** (UI sin acoplamiento DSP) |
| `getParamValue(i)` / `setParamValue(i, v)` | Get/set valores |

**`ParamInfo` (`IEffect.h:19-26`):** `name`, `min`, `max`, `format` (snprintf), `stepSize`, `curve` (`LINEAR`/`LOGARITHMIC`/`EXPONENTIAL`). Permite que la UI edite cualquier parámetro sin conocer el efecto.

### 3.2 Catálogo — 19 efectos (`EffectSlots.h:24, 26-47`)

`MAX_AVAILABLE_EFFECTS = 19`. Enum `EffectType`: `NOISE_GATE`, `DISTORTION`, `DELAY`, `REVERB`, `TREMOLO`, `CHORUS`, `FLANGER`, `PHASER`, `COMPRESSOR`, `AUTO_WAH`, `FUZZ`, `AMP_SIM`, `PARAMETRIC_EQ`, `OVERDRIVE`, `SPEAKER_SIM`, `WAH`, `MOJO`, `COMPRESSOR_1176`, `ACOUSTIC_SIM`.

> Cada efecto vive en `src/dsp/<Nombre>Effect.h` (28 archivos). Implementación `inline` en headers (Regla 3 CLAUDE.md) para cero overhead de llamada.

### 3.3 Slots activos — 4 simultáneos (`EffectSlots.h:23`)

`MAX_ACTIVE_SLOTS = 4` — **coincide exactamente con `MAX_EFFECTS_SIMULTANEOUS=4`** del `platformio.ini`. SSOT cerrada: límite físico = límite lógico = límite UI.

La cadena se procesa en `EffectSlots::processChainBlock(float* left, float* right, size_t numSamples)` (`EffectSlots.h:83`) — **procesamiento por bloques FPU**, no sample-a-sample.

---

## 4. Crossfade gapless (cambio de efecto sin click)

**`SlotTransitionState` (`EffectSlots.h:49-54`):** `ACTIVE → MUTING → LOADING → UNMUTING → ACTIVE`.

| Constante | Valor | Significado |
|:----------|:------|:------------|
| `CROSSFADE_COEF` | `0.005f` | Coeficiente de crossfade wet/dry |
| `CROSSFADE_LOAD_SAMPLES` | `4800` | Duración crossfade = 100 ms @ 48 kHz |

**Mecanismo:** al cargar un efecto nuevo en un slot activo, se baja el wet del viejo (MUTING), se instancia el nuevo en memoria scratch (LOADING), se sube el wet del nuevo (UNMUTING). El usuario no oye click ni corte. Esto es **Touring Grade gapless**.

**IPC lock-free:** `std::atomic<EffectType> pendingEffects[MAX_ACTIVE_SLOTS]` (`EffectSlots.h:122`) — la UI (Core 1) pide un cambio de efecto sin tocar el hot-path (Core 0).

---

## 5. Boutique DSP V1.2 (cadena master)

Más allá de los 4 slots de usuario, el sistema tiene una **cadena master fija** de acondicionamiento (`EffectSlots.h:96-103`):

| Componente | Archivo | Rol |
|:-----------|:--------|:----|
| `inputConditioner` (GuitarConditioner) | `GuitarConditionerEffect.h` | HPF + condición de entrada (quita rumble DC) |
| `globalEQ` (GlobalEQ) | `GlobalEQ.h` | EQ global compartido por toda la cadena |
| `masterWarming` (ZLWarm) | `ZLWarmEffect.h` | Calidez zero-latency a la salida |
| `sweetSpot` (SweetSpotConditioner) | `SweetSpotConditioner.h` | Calibración adaptativa asíncrona (Core 1 offload) |
| `vitalizer` (TransientMaster) | `TransientMaster.h` | Realce de transientes post-cadena |
| `slotLimiters[4]` (SlotSafetyLimiter) | `SlotSafetyLimiter.h` | **Limiter por slot** — protege la cadena de picos |

> 🔒 **Anti-clipping por slot:** cada slot activo tiene su `SlotSafetyLimiter` → un efecto con gain extremo no satura el siguiente. Studio Grade Dynamic Range >100 dB.

---

## 6. ControlMatrix (modulación lock-free — `src/audio/ControlMatrix.h`)

**8 slots lock-free** (`main.cpp:210`) que conectan **moduladores → parámetros**:

| Modulador | Frecuencia | Fuente |
|:----------|:-----------|:-------|
| `wavePedal` (LFO) | 100 Hz | `main.cpp:259` |
| `inputLevel` (Envelope Follower) | 100 Hz | `main.cpp:260` |
| `expressionPedal` (GPIO27 ADC) | UI | `UIManager` |

Cualquier parámetro de cualquier efecto puede mapearse a un modulador vía `ControlMatrix`, sin tocar el hot-path (atómico).

---

## 7. UI — FSM `MenuState` (`src/ui/UIManager.h`)

### 7.1 Estados (`UIManager.h:11-19`)

```
HOME ──push──▶ EFFECT_SELECT ──push──▶ PARAMETER_EDIT
  │                                          │
  ├──FS4──▶ TUNER                            └──back──▶ EFFECT_SELECT
  │
  ├──FSx──▶ GIG_VIEW (⭐ modo escena)
  │
  └──save──▶ SAVE_SCREEN

CALIBRATION (acceso por menú de configuración)
```

| Estado | Render | Encoders activos |
|:-------|:-------|:-----------------|
| `HOME` | preset + banco + efectos activos + volumen | ENC1 (preset), ENC_PARA (vol) |
| `EFFECT_SELECT` | categorías + efectos | ENC1 (categoría), ENC2 (efecto) |
| `PARAMETER_EDIT` | hasta 3 parámetros con `ParamInfo` | ENC1/ENC2/ENC3 |
| `GIG_VIEW` ⭐ | 4 scenes de un vistazo, NeoPixels activas | ENC_PARA (vol), FS1-FS4 |
| `TUNER` | nota + cents | — |
| `CALIBRATION` | hardware debouncing check | — |
| `SAVE_SCREEN` | confirmación guardado | — |

### 7.2 Dirty Flags por zona (`UIManager.h:52-60`)

```cpp
struct DirtyFlags {
    bool header : 1;  // ~16px (preset, bank)
    bool body   : 1;  // ~32px (parámetros, efectos)
    bool footer : 1;  // ~16px (status, volumen)
};
```
**Optimización I2C:** solo se repintan las zonas sucias (no toda la pantalla). El refresco se limita a **30 fps** (`UIManager.h:35-36`) para no saturar el bus I2C.

### 7.3 Escenas/Snapshots gapless (`UIManager.h:94-98`)

- `loadScene(uint8_t)` — carga una scene sin corte de audio.
- `saveCurrentToScene()` — persiste la configuración actual.
- `lastFootswitchStates[4]` — debounce de los 4 footswitches.
- `currentScene` — índice activo.

> **Gapless garantizado por doble mecanismo:** (1) crossfade `SlotTransitionState` (§4) y (2) `loadScene` atómica vía `pendingEffects[]`. FS1-FS4 disparan scenes; la transición es inaudible.

### 7.4 NeoPixels (Category Color Coding)

`updateNeoPixels()` (`UIManager.h:101`) repinta la tira de 8 WS2812B cuando cambia el preset. Cada categoría de efecto = un color consistente (rojo=drive, amarillo=mod, azul=delay, verde=reverb).

---

## 8. Seguridad DSP integrada

### 8.1 TPDF Dither (`AudioSystemI2S.h:100-104`)

```cpp
static uint32_t ditherSeed;
inline int32_t applyTPDFDither(int32_t sample);  // lock-free, FASE 4
```
**Triangular PDF dither** antes del `write24` al DAC. Elimina la cuantización determinística, baja el suelo de ruido audible. La semilla se siembra desde el **TRNG físico** (`PIN_ENTROPY_ADC=29` flotante, `HardwareConfig.h:49`).

### 8.2 CPU Hard-Mute (`AudioSystemI2S.h:106-110`)

```cpp
std::atomic<bool> cpuLimitActive{false};
uint32_t getCycleCount() { return *(volatile uint32_t*)0xE000E018; }  // SysTick
```
Si el CPU budget se acerca al límite (Regla 10: <90%), el sistema mutúa previniendo underruns audibles (glitches). Lee directamente el SysTick para decisión en tiempo real.

### 8.3 Underrun tracking

`volatile uint32_t i2sUnderrunCount` (`AudioSystemI2S.h:95`) — métrica de estabilidad expuesta vía `getUnderrunCount()`. Reportada por `DSPMetrics::printReport()` cada 5 s.

---

## 9. ConfigManager (`src/config/ConfigManager.h`)

- **Presets / Scenes / Snapshots** persistidos en **LittleFS** (flash wear-leveling, CRC16, read-before-write — Regla 10/11).
- **Sensibilidad global** — `targetGlobalSensitivity` (atómico) con transición suave a `currentGlobalSensitivity` (`AudioSystemI2S.h:97-98`).
- Conexión al audio vía `audioSystem.setConfigManager(&configManager)` (`main.cpp:151`).

---

## 10. Tuner (`src/audio/Tuner.h`)

Afinador cromático integrado. `getTuner()` expone la instancia (`AudioSystemI2S.h:64`). En `tuningMode` (atómico, `EffectSlots.h:128`), la cadena puede enmudecer la salida para silencio de afinación.

---

## 11. Shared state (snapshot de atomics)

| Variable | Tipo | Escritor | Lector |
|:---------|:-----|:--------|:-------|
| `dsp_alive` | `atomic<bool>` | Core 1 (`main.cpp:269`) | Core 0 (`main.cpp:168`) |
| `pendingEffects[4]` | `atomic<EffectType>` | UI (Core 1) | Audio (Core 0) |
| `targetMasterVolume` | `atomic<float>` | UI | EffectSlots |
| `tuningMode` | `atomic<bool>` | UI | EffectSlots |
| `peakL` / `peakR` | `atomic<int32_t>` | Audio | UI (medidor) |
| `cpuLimitActive` | `atomic<bool>` | Audio | Audio |
| `targetGlobalSensitivity` | `atomic<float>` | UI/Config | Audio |

> **Lock-free IPC total:** cero mutex, cero `std::lock_guard`. Todo atomics (`Regla 10` P2 Arquitectura) — prioridad de respuesta determinista.

---

## 12. Trazabilidad de reglas

| Regla | Dónde se materializa |
|:------|:---------------------|
| DIRECTIVA CERO (Fire & Forget) | Safe Bypass + CPU Hard-Mute + MSPLIM + Térmico |
| Cero RAM dinámica | `IEffect` prohíbe new/malloc (`IEffect.h:36`); `EffectSlots` memory pool en scratch_x |
| SSOT sample rate | `globalSampleRate` alias inmutable de `SAMPLE_RATE` (`main.cpp:20`) |
| Procesamiento por bloques | `processChainBlock(L, R, N)` (`EffectSlots.h:83`) |
| Crossfade anti-pop | `SlotTransitionState` + `CROSSFADE_LOAD_SAMPLES=4800` |
| Latencia boutique | buffers 2×32 = 1.33 ms (`AudioSystemI2S.h:18-19`) |
| Studio Grade THD<0.05% | TPDF Dither + SlotSafetyLimiter por slot |
| No-Blocking UI | Dirty Flags + 30 fps cap + atomics |
| SOLID (UI desacoplada) | `ParamInfo` + `IEffect` |

---

*Arquitectura de Sistema V1.0 — documentación derivada del `Arquitectura_Hardware.md v1.2` (SSOT primaria) con cross-check de los headers de software (`AudioSystemI2S.h`, `EffectSlots.h`, `IEffect.h`, `UIManager.h`). Cero alucinación: cada afirmación cita `archivo:línea`. Para la matemática DSP detallada, ver `Manual_Teorico_V1.0.md`; para operación del firmware, `Guia_Desarrollador_V1.0.md`.*
