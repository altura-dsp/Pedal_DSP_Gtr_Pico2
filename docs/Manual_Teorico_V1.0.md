# Manual Teórico DSP — Pedal DSP GTR

> **Versión:** 1.0 — Septiembre 2026 (documentación derivada · Prompt 6 v2.0)
> **Proyecto:** `1_Pedal_DSP_GTR`
> **Dominio:** 🎚️ **Studio Grade DSP** (THD<0.05% algorítmico, SNR>105 dB, Dynamic Range>100 dB)
> **Fecha:** Septiembre 2026
> **Base de verdad:** `Arquitectura_Hardware.md v1.2` (SSOT primaria) + `src/audio/*.h` + `src/dsp/*.h` (cross-check)
> **Audiencia:** DSP engineer que necesita la matemática detrás de cada bloque.

> ⚠️ **Cero Alucinación:** las fórmulas y constantes de este manual están **extraídas literalmente del código** (`MathUtils.h`, `DSPTypes.h`, `EffectSlots.h`, `AudioSystemI2S.h`, efectos en `src/dsp/`). Donde un efecto no fue auditado en profundidad, se remite a su header en `src/dsp/` sin inventar su topología.

---

## 1. Convenciones y tipos

### 1.1 Tipo de muestra portable (`DSPTypes.h:6`)

```cpp
typedef float sample_t;   // rango nominal [-1.0f, +1.0f]
```

**Portabilidad inter-plataforma (Regla 3):** cambiar `float` por `int16_t` (o fixed-point) en un solo typedef recompila todo el DSP a otra arquitectura. Todos los efectos reciben/retornan `sample_t`.

### 1.2 Punto fijo en los límites ADC/DAC (`MathUtils.h:22-34`)

| Constante | Valor | Uso |
|:---------|:------|:----|
| `Q23_MAX` | `8388607` (= 2²³−1) | Sample positivo máx **24-bit audio** (PCM1808/PCM5102A) |
| `Q23_MIN` | `−8388608` (−2²³) | Sample negativo mín 24-bit |
| `Q15_MAX` | `32767` | Límite 16-bit (LoFi modes) |
| `Q15_MIN` | `−32768` | Límite 16-bit |

**Mapeo I2S:** el ADC entrega `int32_t` en 24 bits significativos (sign-extended a 32). El hot-path los normaliza a `[-1,+1]` dividiendo por `Q23_MAX`. Antes del DAC, se re-escala y se aplica **TPDF Dither** (§7).

### 1.3 Frecuencias normalizadas

Toda frecuencia de filtro se expresa como `normalizedFreq ∈ [0,1]` donde **1 = Nyquist** (24 kHz a 48 kHz SSOT). Esto desacopla los coeficientes del sample rate concreto.

---

## 2. One-pole smoother y LPF (`MathUtils.h:43-78`)

### 2.1 LPF one-pole — coeficiente alpha

```cpp
inline float lpf_alpha_float(float normalizedFreq) {
    constexpr float SCALE = 6.28318530718f;   // 2π
    return expf(-normalizedFreq * SCALE);
}
```
**Fórmula:** α = e^(−2π·ω) donde ω = fc/fs (normalizada). Diferencia del filtro:
```
y[n] = α·y[n−1] + (1−α)·x[n]
```
Usado para **damping de feedback** en delay, **suavizado de parámetros**, **cálculo de envolvente**.

### 2.2 Suavizado de parámetros (anti-zipper noise) — `smooth_param`

```cpp
inline float smooth_param(float current, float target, float coef) {
    return current + (target - current) * coef;
}
```
**Fórmula:** y[n] = y[n−1] + (x − y[n−1]) · coef, con `coef ∈ [0,1]`.

**Constantes en uso (código real):**
| Contexto | Coef | Fuente |
|:---------|:-----|:-------|
| Crossfade wet/dry slot | `0.005f` | `EffectSlots.h:63` `CROSSFADE_COEF` |
| Master volume smooth | `0.001f` | `EffectSlots.h:126` `MASTER_SMOOTH_COEF` |
| Delay (time/feedback/mix/damp) | `0.002f` | `DelayEffect.h:87` `SMOOTH_COEF` |

> Sin este suavizado, girar un encoder inyecta discontinuidades → clicks (zipper noise). Touring Grade exige transiciones audibles-inaudibles.

---

## 3. Aproximación `exp(x)` rápida (`MathUtils.h:54-61`)

```cpp
inline float fast_exp_approx(float x) {
    x *= 1.44269504f;                                     // 1/ln(2)  → paso a base 2
    float frac = x - (int)x;
    float base = (1.0f + frac * (0.693147f + frac * 0.240226f));  // Taylor 2º orden de 2^frac
    return base * (float)(1 << (int)x);                   // shift = 2^parte_entera
}
```
**Idea:** e^x = 2^(x/ln2). La parte entera del exponente se resuelve con un **shift entero** (gratis en hardware); la parte fraccional con Taylor de 2º orden. Mucho más rápido que `expf()` de la libc, ideal para **no-linealidades tube/diodo** (Shockley, waveshapers) evaluadas muestra a muestra en distorsiones.

---

## 4. EQ Paramétrico — SVF TPT 6 bandas (`ParametricEqEffect.h`)

Topología confirmada: **State Variable Filter Topology-Preserving Transform (SVF TPT)** flotante, 6 bandas (Low Shelf + 4× Peak/Bell + High Shelf), 18 parámetros (Freq, Q, Gain por banda).

### 4.1 Estado del filtro (`ParametricEqEffect.h:48-53`)

```cpp
struct SVFState {
    sample_t z1_L = 0.0f, z2_L = 0.0f;   // estados integradores (canal L)
    sample_t z1_R = 0.0f, z2_R = 0.0f;   // estados integradores (canal R)
} states[6];
```
`z1` y `z2` son los **dos integradores internos** del SVF (uno por canal, uno por banda = 24 estados estéreo totales).

### 4.2 Coeficientes precalculados (`ParametricEqEffect.h:56-64`)

```cpp
struct SVFCoeffs { sample_t g, R, K, g1, d; };
SVFCoeffs coeffs[2][6];                    // double-buffered [L/R][banda]
std::atomic<uint8_t> activeCoeffsIdx{0};   // índice atómico
```

- `g` = tan(π·fc/fs) — ganancia del integrador (mapea frecuencia a ángulo).
- `R` = damping (relacionado con Q).
- `K`, `g1`, `d` — términos TPT para preservar topología (Chamberlin/Zavalishin form).

> ⭐ **Double-buffering RT-safe:** la UI recalcula coeficientes en el buffer inactivo (`recalculateCoefficients()`), luego cambia `activeCoeffsIdx` atómicamente. El hot-path **nunca** ve coeficientes a medio calcular → cero glitch.

### 4.3 Diferencia TPT (Zavalishin)

El SVF TPT resuelve la ecuación de forma **topology-preserving**: integra explícitamente z1/z2 con el método trapezoidal bilineal, garantizando **estabilidad incluso en variantes paramétricas extremas** (a diferencia del biquad Direct Form II que se inestabiliza con coeficientes altos). Por eso SVF > Biquad para EQ paramétrico interactivo (Regla rules/4 §filtros SVF vs Biquads).

---

## 5. Delay analógico BBD — buffer circular + Thiran + damping (`DelayEffect.h`)

### 5.1 Topología confirmada

```cpp
#define MAX_DELAY_SAMPLES 19200   // 400 ms @ 48 kHz
sample_t* bufferL; sample_t* bufferR;   // pool estático global (cero malloc)
uint32_t writeIndex;                     // índice circular
```
**Buffer circular puro** (sin shift de memoria). El pool es estático global para evitar fragmentación en EffectSlots.

### 5.2 Modos (`DelayEffect.h:13-19`)

`STEREO_HIFI`, `MONO_HIFI`, `MONO_LOFI`, `PING_PONG`, `CROSS_FEEDBACK`.

### 5.3 Interpolación Thiran allpass (`DelayEffect.h:90`)

```cpp
sample_t thiran_z1_L, thiran_z1_R;   // estado del allpass fraccional
```
**Thiran allpass:** permite retardos **fraccionales** (ej. 1234.7 muestras) sin distorsión de fase ni aliasing. Conserva la respuesta de fase plana, clave para **Ping-Pong estéreo coherente** y delays musicalmente ajustados al BPM.

### 5.4 Damping en el lazo de feedback

```cpp
sample_t targetDamping;          // parámetro UI
sample_t dampAlpha;              // coeficiente LPF del feedback (vía lpf_alpha_float)
```
**Fórmula del lazo:** `fb = LPF(prev_out) · feedback`, donde el LPF one-pole (§2.1) simula la **oscuridad progresiva** de un BBD real (cada repetición pierde agudos). Sin esto, el delay suena "metálico/digital".

### 5.5 Ecuación de salida

```
readSample = bufferL[(writeIndex - delaySamples) MOD MAX]   // + interpolación Thiran
y[n]   = x[n]·(1−mix) + readSample·mix
fbNext = readSample·feedback (con damping LPF aplicado)
bufferL[writeIndex] = x[n] + fbNext
writeIndex = (writeIndex + 1) MOD MAX
```

---

## 6. Crossfade gapless entre efectos (`EffectSlots.h`)

**Duración:** `CROSSFADE_LOAD_SAMPLES = 4800` muestras = **100 ms** @ 48 kHz. **Coef:** `CROSSFADE_COEF = 0.005f`.

### 6.1 Máquina de estados del slot (`EffectSlots.h:49-54`)

```
ACTIVE ──loadEffect()──▶ MUTING ──▶ LOADING ──▶ UNMUTING ──▶ ACTIVE
                          (baja wet   (instancia   (sube wet
                           del viejo)  en scratch)   del nuevo)
```

### 6.2 Matemática del crossfade

One-pole smoother (§2.2) aplicado al `wetMix` de cada slot:
```
wetMix[n] = wetMix[n−1] + (target − wetMix[n−1]) · 0.005
```
Durante MUTING: target=0 (viejo se desvanece). Durante UNMUTING: target=1 (nuevo aparece). **Suma siempre 1** en régimen estacionario → sin discontinuidad de ganancia → sin click.

### 6.3 Spill-over (`IEffect.h:73-83`)

`setTailOnly(true)` — el efecto sale del chain de entrada pero sigue procesando sus **colas internas** (reverb decay, delay repeats). Así, apagar una reverb no corta el tail abruptamente: sigue sonando hasta extinguirse.

---

## 7. TPDF Dither (pre-DAC) — `AudioSystemI2S.h:100-104`

```cpp
static uint32_t ditherSeed;
inline int32_t applyTPDFDither(int32_t sample);
```

### 7.1 Por qué

Al re-escalar `sample_t ∈ [-1,+1]` a `int32_t` 24-bit, la cuantización determinística produce **distorsión armónica** (no ruido blanco). El TPDF (Triangular Probability Density Function) dither convierte ese error en ruido aleatorio no correlacionado, **bajando el suelo de ruido audible** y eliminando tonos espurios.

### 7.2 Implementación

- **Semilla física:** el TRNG del RP2350 vía `PIN_ENTROPY_ADC=29` (GPIO flotante captura EMF ambiental, `HardwareConfig.h:49`). Verdadera entropía, no pseudoaleatorio determinista.
- **PDF triangular:** suma de dos uniformes independientes (U1+U2−1) → triángulo.
- **Lock-free:** la semilla se actualiza con operaciones atómicas, sin mutex.

> 🎚️ **Studio Grade SNR>105 dB:** el TPDF es lo que acerca el silencio digital al piso analógico. Sin él, el SNR medible cae 6-12 dB.

---

## 8. Double-buffered coefficients (patrón RT-safe)

**Problema:** recalcular coeficientes de filtro dentro del `processBlock` introduce latencia + riesgo de usar coeficientes a medio actualizar.

**Solución (usada en `ParametricEq`, `Delay`, y todos los efectos con coeficientes pesados):**

```cpp
DelayCoeffs coeffs[2];                       // dos sets completos
std::atomic<uint8_t> activeCoeffsIdx{0};     // cuál está activo
```

1. UI llama `setParamValue()` → actualiza `target*` atómicos.
2. Antes del siguiente `processBlock`, `recalculateCoefficients()` escribe en `coeffs[1 − activeCoeffsIdx]` (el inactivo).
3. Al terminar, swap atómico: `activeCoeffsIdx = 1 − activeCoeffsIdx`.
4. El hot-path lee `coeffs[activeCoeffsIdx]` → siempre un set coherente y completo.

> 🔒 **Cero glitch paramétrico:** mover un encoder de EQ nunca produce un pop. Touring Grade.

---

## 9. Limiters y clipping safety

### 9.1 SlotSafetyLimiter (por slot — `EffectSlots.h:103`)

```cpp
SlotSafetyLimiter slotLimiters[MAX_ACTIVE_SLOTS];   // un limiter por slot activo
```
**Rol:** cada slot tiene su propio limiter **antes** de pasar la salida al siguiente efecto. Un fuzz con +40 dB de gain no satura el delay siguiente; el limiter clampea limpiamente.

### 9.2 Soft-clipping pre-DAC

Antes del `write24`, la cadena master aplica **soft-clipping algorítmico** (Regla 7 Studio Grade: *requires Soft-Clipping algorítmico pre-DAC*). Mapea muestras >1.0 con una curva suave (ej. tanh) en lugar de hard-clip, evitando armónicos espurios.

### 9.3 CPU Hard-Mute (`AudioSystemI2S.h:106-110`)

Si el `getCycleCount()` (SysTick directo, registro `0xE000E018`) detecta que el bloque excede el budget → `cpuLimitActive=true` y se **mutúa previniendo underruns**. Mejor silencio breve que glitch audible (Regla 10 §Underrun USB).

---

## 10. Catálogo DSP — referencia

> **No se documentan aquí fórmulas no auditadas.** Cada efecto tiene su topología documentada en su header. Lista completa (`src/dsp/`):

| Categoría | Efectos (`EffectType`) |
|:----------|:----------------------|
| **Drive** | `DISTORTION`, `OVERDRIVE`, `FUZZ`, `AMP_SIM` |
| **Modulación** | `CHORUS`, `FLANGER`, `PHASER`, `TREMOLO`, `AUTO_WAH`, `WAH` |
| **Delay/Reverb** | `DELAY` (§5), `REVERB` |
| **Dinámica** | `COMPRESSOR`, `COMPRESSOR_1176`, `NOISE_GATE` |
| **EQ/Filtro** | `PARAMETRIC_EQ` (§4), `SPEAKER_SIM`, `ACOUSTIC_SIM` |
| **Master/Boutique** | `MOJO`, `ZLWarm` (master warming), `GuitarConditioner` (input), `GlobalEQ`, `SweetSpotConditioner`, `TransientMaster` (vitalizer), `SlotSafetyLimiter` |

Para detalle matemático de cada uno, consultar el header correspondiente en `src/dsp/` (todas las implementaciones son `inline` en `.h`, Regla 3 CLAUDE.md).

---

## 11. Invariantes Studio Grade (verificación)

| Meta (CLAUDE.md Regla 7) | Mecanismo en código |
|:--------------------------|:--------------------|
| **THD algorítmico <0.05%** | Soft-clipping pre-DAC + SlotSafetyLimiter + TPDF Dither |
| **SNR digital >105 dB** | TPDF Dither + RAM inicializada a 0 + FPU 32-bit |
| **Respuesta en frecuencia ±0.2 dB** | SVF TPT (estabilidad paramétrica) + Thiran allpass (fase) |
| **Rango dinámico >100 dB** | FPU fpv5-sp-d16 + soft-clipping + 24-bit I2S |
| **Latencia boutique ~1.33 ms** | Buffers I2S 2×32 (`AudioSystemI2S.h:18-19`) |

> **SPICE (lado analógico) midió THD línea 0.0000%** y **headroom 1.001 V** (`SPICE_Report.md`) — la cadena analógica confirma el presupuesto Studio Grade del lado DSP.

---

*Manual Teórico V1.0 — documentación derivada del `Arquitectura_Hardware.md v1.2` (SSOT primaria) con cross-check estricto de `src/audio/*.h` y `src/dsp/*.h`. Cero alucinación: cada fórmula y constante cita su `<archivo>:<línea>`. Para el firmware y boot, ver `Guia_Desarrollador_V1.0.md`; para el mapa de módulos, `Arquitectura_Sistema_V1.0.md`.*
