# Guía del Desarrollador — Pedal DSP GTR

> **Versión:** 1.0 — Septiembre 2026 (documentación derivada · Prompt 6 v2.0)
> **Proyecto:** `1_Pedal_DSP_GTR`
> **Dominio:** 🎸 **Touring Grade** + **Studio Grade DSP** (Código/DSP)
> **Fecha:** Septiembre 2026
> **Base de verdad:** `Arquitectura_Hardware.md v1.2` (SSOT primaria) + `src/` (cross-check) + `rules/9` (protocolo)
> **Audiencia:** desarrollador firmware / DSP engineer que va a modificar, extender o depurar el pedal.

---

## 1. Visión general del firmware

El pedal corre sobre **Raspberry Pi Pico 2 (RP2350 dual-core Cortex-M33 @ 196.608 MHz, FPU fpv5-sp-d16)** con Arduino-Pico framework. **Dual-core estricto:**

```
┌─────────────────────────────────── Core 0 (Audio · Hot-Path) ───────────────────────────────────┐
│  setup()  → boot seguro (mute, vreg, watchdog, MCLK, I2S DMA, NVIC prio 0x00)                   │
│  loop()   → isr_hardfault() · heartbeat watchdog · Safe Bypass state machine · audioSystem.processAudio()│
└──────────────────────────────────────────────────────────────────────────────────────────────────┘
┌─────────────────────────────────── Core 1 (UI · Telemetría) ─────────────────────────────────────┐
│  setup1() → FTZ (denormals kill) · DSPMetrics · ADC temp sensor · ControlMatrix · UIManager      │
│  loop1()  → térmico jerárquico (85/95 °C) · DSPMetrics reporte · moduladores 100 Hz · UI · heartbeat│
└──────────────────────────────────────────────────────────────────────────────────────────────────┘
                    IPC: std::atomic<bool> dsp_alive  (Core 1 → Core 0, lock-free)
```

**Principios forzados (CLAUDE.md):**
- **Cero RAM dinámica** en hot-path (`alignas(32) AudioSystemI2S audioSystem` en `main.cpp:23`).
- **Punto Único de Verdad:** `HardwareConfig.h` centraliza pines; `SAMPLE_RATE` es `constexpr` con alias inmutable `globalSampleRate` (`main.cpp:20`).
- **No-Blocking UI:** `delay()` solo en `setup()` boot (P1 Safety) y en mute térmico 100 ms (Core 1, no hot-path).
- **Fire & Forget:** MSPLIM stack guard + Safe Bypass + Mute Relay + FaultLatch + térmico jerárquico.

---

## 2. Árbol de fuentes (referencia)

```
src/
├── main.cpp                       ← entry point, dual-core split (Core 0 audio / Core 1 UI)
├── audio/
│   ├── AudioSystemI2S.{h,cpp}     ← I2S 24-bit DMA estéreo, read24/write24, processAudio()
│   ├── EffectSlots.{h,cpp}        ← 8 slots lock-free (MAX_EFFECTS_SIMULTANEOUS=4 activos)
│   ├── ControlMatrix.{h,cpp}      ← matriz de modulación (moduladores → parámetros)
│   └── sweetSpot/                 ← calibración asíncrona (Core 1 offloading)
├── ui/
│   └── UIManager.{h,cpp}          ← MenuState FSM (HOME→EFFECT_SELECT→…→GIG_VIEW), Dirty Flags, NeoPixels
├── config/
│   ├── HardwareConfig.h           ← SSOT de pines (NO tocar sin actualizar platformio.ini)
│   └── ConfigManager.{h,cpp}      ← sensibilidad global, presets, LittleFS
├── system/
│   └── FaultLatch.{h,cpp}         ← registro persistente de faults (STACK_OVERFLOW, THERMAL_*)
└── utils/
    └── DSPMetrics.{h,cpp}         ← temperatura, CPU budget, reporte Serial
```

---

## 3. Build y configuración (`platformio.ini`)

**Parámetros clave (leídos del `platformio.ini`):**
- `board = pico2` (RP2350, módulo Pico 2).
- `board_build.f_cpu = 196608000L` → **196.608 MHz overclock matemáticamente derivado** (múltiplo de 48 kHz y 12.288 MHz MCLK).
- `build_flags`:
  - `-D MAX_EFFECTS_SIMULTANEOUS=4` → límite de efectos activos a la vez (Studio Grade constraint).
  - `-D PIN_I2S_BCLK=14` / `PIN_I2S_LRCLK=15` / `PIN_I2S_DIN=17` / `PIN_I2S_DOUT=16` → espejo de `HardwareConfig.h` (anti-colisión física SSOT).
- `lib_deps`: Adafruit SSD1306 + Adafruit GFX Library (driver OLED).

> ⚠️ **SSOT doble espejo (Regla 3 CLAUDE.md):** los pines viven en `HardwareConfig.h` (compilación) **Y** en `platformio.ini` (build flags `-D`). Si cambias un pin, **debes tocar ambos** o el `.ini` pisa el `.h`. El motor Hardware-As-Code (`rules/12`) automatiza esto; en hand-edits, disciplina manual.

**Comandos de build:**
```bash
pio run                     # compila (cold build)
pio run -t upload           # flashea Pico 2 vía USB
pio device monitor -b 115200 # telemetría Serial (DSPMetrics reporte cada 5 s)
```

---

## 4. `HardwareConfig.h` — el SSOT de pines

> Namespace `config::hardware`. Todo acceso a pines en el firmware pasa por aquí. **Cero Magic Numbers.**

| Símbolo | Valor | Propósito | Línea |
|:--------|:------|:----------|:------|
| `SAMPLE_RATE` | `48000.0f` | Frecuencia SSOT Studio Grade | `HardwareConfig.h:24` |
| `PIN_I2S_BCLK` | `14` | Bit clock I2S (ADC+DAC) | `HardwareConfig.h:29` |
| `PIN_I2S_LRCLK` | `15` | Word select L/R (= BCLK+1) | `HardwareConfig.h:33` |
| `PIN_I2S_DIN` | `17` | Data in ← PCM1808 ADC | `HardwareConfig.h:37` |
| `PIN_I2S_DOUT` | `16` | Data out → PCM5102A DAC | `HardwareConfig.h:41` |
| `PIN_ENTROPY_ADC` | `29` | TRNG — **DEBE quedar flotante** en el PCB | `HardwareConfig.h:49` |
| `PIN_MUTE_RELAY` | `23` | Relé mute (NC a tierra en boot) | `HardwareConfig.h:54` |
| `ADC_CHANNEL_TEMP` | `4` | **Canal ADC interno** (no GPIO4 físico) | `HardwareConfig.h:55` |
| `PIN_BYPASS_CTRL` | `24` | Safe Bypass HEF4053BT (HIGH=bypass ON) | `HardwareConfig.h:56` |

**Macro de aritmética segura (SSOT `lib_DSP/core/DSPTypes.h:28`):**
```cpp
// Ternary pura — prohibido clamp/epsilon (Regla 15 / anti-patrón A16).
// Se CONSUME vía lib_extra_dirs; prohibido redefinir localmente.
#define SAFE_DIVIDE(num, den) ((den) != 0.0f ? ((num) / (den)) : 0.0f)
```

> 🔒 **D5 (saneamiento naming):** `ADC_CHANNEL_TEMP=4` es canal interno del ADC del RP2350, **NO** el GPIO4 físico (que es `ENC1_SW`). El naming previo colisionaba y confundía. No renombrar.

---

## 5. Secuencia de boot (`setup()` Core 0 — `main.cpp:101-158`)

**Fire & Forget: cada paso tiene un porqué.**

| Paso | Código | Porqué (P-prioridad CLAUDE.md) |
|:-----|:-------|:-------------------------------|
| Serial 115200 | `main.cpp:102` | Telemetría DSPMetrics |
| VREG 1.10 V | `main.cpp:106` | Sostener overclock 196 MHz (P1 Safety — corrupción memoria si cae tensión) |
| Mute Relay LOW | `main.cpp:110-111` | Silencio absoluto al arranque (P1 — anti-pop amplificador) |
| `delay(500)` | `main.cpp:114` | Único delay permitido: estabilización fuente (P1 > P2 No-Blocking) |
| Mute Relay HIGH | `main.cpp:117` | Libera el audio tras estabilización |
| Bypass CTRL LOW | `main.cpp:121-122` | Modo DSP (bypass OFF); HIGH = paso directo analógico |
| Watchdog 8000 ms | `main.cpp:126` | Reset si cuelgue >8 s (margen estrés 196 MHz) |
| MCLK 12.288 MHz | `main.cpp:131-136` | `clk_gpout0` en GPIO21 — anti-jitter matemático 256×48k |
| `audioSystem.init()` | `main.cpp:141` | I2S DMA estéreo; halt si falla (`while(true)`:143) |
| NVIC prio DMA_IRQ_0 = 0x00 | `main.cpp:147-148` | Máxima prioridad al I2S (Regla 10) |
| `setConfigManager` | `main.cpp:151` | Sensibilidad global wired al audio |
| `setInputLevel` | `main.cpp:154` | Envelope follower (Core 0 hot-path) |

**Setup1 Core 1 (`main.cpp:188-215`):**
- **FTZ (Flush-to-Zero)** bit 24 del FPSCR (`main.cpp:193-196`) — mata denormals que congelan la FPU (P3).
- `DSPMetrics::init()` + `adc_set_temp_sensor_enabled(true)` (`main.cpp:199, 203`).
- `ControlMatrix::init()` (8 slots lock-free) + `wavePedal` + `inputLevel` (`main.cpp:206-208`).
- `uiManager.init()` — OLED (`main.cpp:212`).

---

## 6. Hot-path (`loop()` Core 0 — `main.cpp:160-183`)

```
loop():
  1. isr_hardfault()                       ← MSPLIM (hardware limit) (P1)
  2. dsp_alive.exchange(false)          ← heartbeat atómico del Core 1
       ├── si true  → watchdog_update() · bypass=LOW (audio por DSP)
       └── si >3 s  → bypass=HIGH       ← Safe Bypass: audio NUNCA se detiene (P1)
  3. audioSystem.processAudio()         ← I2S DMA read24/write24, ~48 kHz
```

> ⭐ **Safe Bypass state machine (`main.cpp:166-178`):** Core 1 marca `dsp_alive=true` cada iteración. Si Core 0 no ve el pulso por >3000 ms (DSP caído), **sin reboot abrupto**: conmuta `PIN_BYPASS_CTRL=HIGH` y el audio pasa por el path analógico HEF4053BT. El watchdog (8 s) reinicia solo si persiste. Regla CLAUDE.md 5: *el audio nunca debe detenerse en un show*.

---

## 7. UI + Telemetría + Moduladores (`loop1()` Core 1 — `main.cpp:217-270`)

**Ciclo cada `now`:**
1. **Térmico jerárquico cada 5 s** (`main.cpp:227`):
   - `updateTemperature()` lee ADC canal interno.
   - **95 °C → CRÍTICO** (`main.cpp:231-237`): `registerFault(THERMAL_CRITICAL)` · `bypass=HIGH` · `emergencyMute()` · `while(true)` sin `watchdog_update` → reboot controlado en 8 s tras enfriar.
   - **85 °C → ADVERTENCIA** (`main.cpp:240-246`): `mute=LOW · delay(100) · mute=HIGH` (mute preventivo breve) + `registerFault(THERMAL_THROTTLE)`.
2. **Reporte métricas** cada `PRINT_INTERVAL_MS` (`main.cpp:252`).
3. **Moduladores a 100 Hz** (`main.cpp:257-260`): `wavePedal.process()` + `inputLevel.updateMatrix()`.
4. **Calibración asíncrona** (`main.cpp:263`): `sweetSpot.processAsyncCalibrationCore1()` (offloading Core 1).
5. **UI update** (`main.cpp:266`): FSM `UIManager::update()` a 30 fps.
6. **Heartbeat** (`main.cpp:269`): `dsp_alive.store(true)`.

---

## 8. Seguridad RT (sistema inmunológico del pedal)

### 8.1 Stack MSPLIM (`main.cpp:87-91`)

El SDK RP2350 activa **MSPLIM** (Main Stack Pointer Limit, ARMv8-M nativo del Cortex-M33) en `runtime_init_stack_guard.c` al arrancar — **cero configuración manual, cero overhead**. Si el Stack Pointer baja del límite, el hardware dispara **HardFault** en el ciclo exacto (sin polling ni canarios de software). Se intercepta con:

```cpp
extern "C" void isr_hardfault(void) {
    emergencyMute();            // silencia I2S (~1µs, escritura directa a registros)
    watchdog_reboot(0, 0, 0);   // reboot determinista vía Watchdog
    while (true) {}             // nunca llega aquí
}
```

El handler es **minimal y deliberado**: `isr_hardfault` es una **ISR de HardFault** (no corre en `loop()`). **BUG FIX #4 (`main.cpp:87`):** en pila corrupta, cualquier llamada a función —incluido `Serial`— puede causar un HardFault en cascada, por eso **no registra en FaultLatch ni imprime**: solo silencia y reinicia. Los canarios de software (`0xDEADBEEF`) + `checkStack()` quedaron **obsoletos** (rules/10 §1.1).

### 8.2 Emergency Mute (`main.cpp:54-64`)

Fuerza `PIN_I2S_BCLK` y `PIN_I2S_DOUT` a LOW antes de cualquier reset, previniendo ráfagas espurias en el DAC. **Sin delay** (el cambio GPIO es instantáneo — se eliminó un `delay(10)` innecesario).

### 8.3 FaultLatch (`system/FaultLatch.{h,cpp}`)

Registro persistente de faults: `STACK_OVERFLOW`, `THERMAL_CRITICAL`, `THERMAL_THROTTLE`. Permite diagnóstico post-reset (ver `Mantenimiento_Troubleshooting_V1.0.md`).

### 8.4 Watchdog Heartbeat atómico

`std::atomic<bool> dsp_alive{false}` (`main.cpp:33`) — comunicación inter-core **lock-free** (Regla 10). Core 1 escribe, Core 0 lee con `exchange(false)` (test-and-clear).

---

## 9. Métricas DSP (`utils/DSPMetrics`)

- `DSPMetrics::init()` en `setup1()` (`main.cpp:199`).
- `updateTemperature()` cada 5 s (`main.cpp:228`).
- `checkThermalCritical()` / `checkThermalWarning()` — umbrales 95/85 °C.
- `printReport()` cada `PRINT_INTERVAL_MS` por Serial (`main.cpp:252`).
- Reporta: temperatura, CPU budget (objetivo <90% Regla 10), underruns DMA.

---

## 10. Cómo extender el firmware (recetas KISS)

### 10.1 Añadir un efecto nuevo

1. Crea `src/audio/effects/MiEfecto.h` con `inline float processSample(float in, MiEfectoContext& ctx)`.
2. Regístralo en `EffectSlots` (máximo `MAX_EFFECTS_SIMULTANEOUS=4` activos).
3. Expón sus parámetros vía `ControlMatrix` (slot lock-free).
4. Asigna categoría en `UIManager` para el Category Color Coding NeoPixel.
5. **Cero `new`/`malloc` en el processSample** (P2). Usa `static struct` para estado.

### 10.2 Cambiar un pin

1. Edita **`HardwareConfig.h`** (símbolo `config::hardware::PIN_*`).
2. Edita **`platformio.ini`** (`-D PIN_*=nuevo`).
3. Re-rutea el `.md` SSOT (Fase 2) y re-validad SPICE.
4. **Nunca** hardcoded `pinMode(n, ...)` disperso por el código.

### 10.3 Subir sample rate (avanzado)

`SAMPLE_RATE` es `constexpr` con alias `globalSampleRate`. Cambiar en `HardwareConfig.h:24` **y** recalcular MCLK (`12.288 MHz = 256 × 48 kHz`). Validar CPU budget y filtros anti-aliasing analógicos.

---

## 11. Reglas Fire & Forget aplicadas (trazabilidad CLAUDE.md)

| Regla CLAUDE.md | Dónde se aplica | Línea clave |
|:-----------------|:----------------|:------------|
| DIRECTIVA CERO (Fire & Forget) | Safe Bypass + Mute + MSPLIM + FaultLatch | `main.cpp:166-178, 83-94` |
| P1 Safety (Mute Relay + Boot) | `delay(500)` + Mute LOW→HIGH | `main.cpp:110-117` |
| P1 Safety (Térmico) | 85/95 °C jerárquico | `main.cpp:231-246` |
| P1 Safety (BOD/VREG) | `vreg_set_voltage(VREG_VOLTAGE_1_10)` | `main.cpp:106` |
| P2 Arquitectura (Cero RAM dinámica) | `alignas(32)` AudioSystem estático | `main.cpp:23` |
| P2 Arquitectura (SSOT pines) | `HardwareConfig.h` namespace | `HardwareConfig.h:18-58` |
| P2 Arquitectura (No-Blocking UI) | `delay()` solo P1/Core 1 | `main.cpp:114, 242` |
| P3 DSP (Aritmética segura) | `SAFE_DIVIDE` macro | `HardwareConfig.h:1-3` |
| P3 DSP (Denormals FTZ) | bit 24 FPSCR Core 1 | `main.cpp:193-196` |
| P3 DSP (Stack Overflow) | MSPLIM + isr_hardfault() | `main.cpp:40-46, 83-94` |
| Regla 5 (Watchdog Heartbeat) | `dsp_alive` atómico | `main.cpp:33, 168-178` |
| Regla 10 (NVIC prio I2S) | DMA_IRQ_0 = 0x00 | `main.cpp:147-148` |

---

## 12. Auditoría rápida (checklist antes de commit)

- [ ] ¿Nuevo código toca algún pin sin pasar por `HardwareConfig.h`? ❌
- [ ] ¿Hay `new`/`malloc` en hot-path (audio)? ❌
- [ ] ¿Hay `delay()` fuera de setup/mute-térmico? ❌
- [ ] ¿División sin `SAFE_DIVIDE` o garantía de divisor mínimo? ❌
- [ ] ¿Array local >256 elementos en hot-path? ❌ (usar `static` o init heap)
- [ ] ¿MSPLIM intacto (sin HardFaults) tras stress test? ✅
- [ ] ¿CPU budget <90% en `DSPMetrics::printReport()`? ✅
- [ ] ¿SSOT `.md` ↔ código ↔ `platformio.ini` coherentes (sin A16)? ✅

---

*Guía del Desarrollador V1.0 — documentación derivada del `Arquitectura_Hardware.md v1.2` (SSOT primaria) con cross-check del código fuente (`main.cpp`, `HardwareConfig.h`, `platformio.ini`). Cero alucinación: cada afirmación cita `archivo:línea`. Para la matemática DSP detallada, ver `Manual_Teorico_V1.0.md`.*
