# Especificaciones de Hardware — 1_Pedal_DSP_GTR

> **Versión:** 1.2 (ECO 12 V aplicado — alineado a `Arquitectura_Hardware.md` v1.2 · decisiones de arquitectura resueltas · canon Hardware-As-Code v1.3)
> **Fecha:** 21 Julio 2026
> **Target:** Raspberry Pi Pico 2 (RP2350, Cortex-M33 Dual-Core)
> **Dominio:** 🎸 **Touring Grade** (pedal multi-efectos de guitarra en vivo)

Este documento es la **Fuente Única de Verdad (SSOT)** para la arquitectura física, ruteo analógico y restricciones de hardware del proyecto **1_Pedal_DSP_GTR**. Es la semilla que alimentará a `Arquitectura_Hardware.md` (Fase 2 del pipeline Hardware-As-Code). **Topología, chips y protecciones — sin ruteos pin-a-pin de SKiDL** (eso es trabajo de la Fase 2).

---

## 1. Topología Analógica (DOMINIO: Touring Grade)

La etapa analógica está diseñada para **coloración vintage controlada** + **transparencia en el path seco**: preamp JFET de alta impedancia para pastillas de guitarra, procesado DSP a 48 kHz/24-bit, y salida de línea con headroom de escena. El path seco respeta THD+N < 0.1 % (regla 7.0.1); los efectos (AmpSim, Fuzz, Distortion) inyectan distorsión **musical intencional**.

### 1.1 Entradas Analógicas (1 canal instrumento + 1 expresión)

- **Canal 1 (Instrumento — Guitarra):** Preamplificador **TL072** (opamp dual JFET, GBW 3 MHz, Rin 1 TΩ).
  - **Justificación ROI (regla 7.2):** pedal vintage → coloración y **alta impedancia JFET de entrada** para no cargar las pastillas (que tienen Z_out ~10–20 kΩ). El OPA1612 queda **prohibido aquí** (es para interfaces transparentes, no pedales vintage).
  - **Protecciones:** TVS bidireccional **P6KE18CA** (BV 17.1 V) en la entrada de jack; condensador de acople DC wideband **10 µF ‖ 100 nF** (cubre banda de audio + transitorios).
  - **Gain Staging Thévenin (referencia regla 7.0.1):** divisor **15 kΩ / 4.7 kΩ** con carga de **20 kΩ** del ADC PCM1808 →
    - $V_{out} = V_{in} \cdot \frac{4.7\text{k} \parallel 20\text{k}}{15\text{k} + (4.7\text{k} \parallel 20\text{k})} = V_{in} \cdot \frac{3.78}{18.78} \approx V_{in} \cdot 0.201$
    - Para $V_{in} = 9.2\text{Vpp}$ (pico de pastilla humbucker hot tras TL072 gain ×5): $V_{ADC} \approx 1.85\text{Vpp} < 3.0\text{Vpp}$ (full-scale PCM1808). ✅ Sin saturación, 6 dB de headroom.
- **Canal 2 (Pedal de Expresión):** **PIN_EXPRESSION = GPIO27** (ADC1). Potenciómetro externo 10 kΩ → divisor resistivo → ADC. No requiere preamp (señal DC lenta).
- **Entrada ADC:** **PCM1808** (TI, 24-bit, 96 kHz máx, I2S). Ambos canales convergen al codec vía I2S (`PIN_I2S_DIN=17`, `PIN_I2S_BCLK=14`, `PIN_I2S_LRCLK=15`).

### 1.2 Salidas Analógicas (2 zonas)

- **DAC:** **PCM5102** (TI, 24-bit, I2S, Ripple-Rejection integrado, formato left-justified/I2S seleccionable). XSMT manejado para mute silencioso.
  - **MCLK 12.288 MHz** generado en hardware desde el RP2350: `clock_configure(clk_gpout0, ...)` y ruteado a `PIN_I2S_MCLK = GPIO21` (`main.cpp:131-136`). Reloj matemático anti-jitter (256·fs).
- **Salida Principal ESTÉREO (Línea / Pedal Loop):** Buffer **NE5532** (opamp dual bipolar, GBW 10 MHz, Slew 9 V/µs, ilimit ±38 mA) — usa las **dos mitades del dual para L+R**. Salida **estéreo** no balanceada (L/R) a jacks TRS 6.35 mm para loop de efectos o entrada a amplificador. **Decisión de arquitectura: ESTÉREO** — coherente con el firmware `AudioSystemI2S.cpp` que ya procesa `read24(&inLeft,&inRight)` / `write24(outL,outR)` y `processChainBlock(fLeft, fRight, …)` en cadena DSP estéreo (peakL/peakR atómicos separados).
  - **Justificación:** NE5532 es el opamp canónico de salida por su baja distorsión + drive suficiente para cargas de línea (regla 7.2).
- **Salida Secundaria (Auriculares):** Amplificador de alta corriente **NJM4556A** (Nisshinbo, **±73 mA high-current**, Rout 10 Ω) dedicado al jack de auriculares.
  - **Justificación:** el NE5532 (±38 mA) no drivea auriculares de 32 Ω con headroom; el NJM4556A sí. (Modelo SPICE: wrapper fiel con ilimit=73 mA — `spice_models/NJM4556A.lib`.)

---

## 2. Microcontrolador y Relojes (RP2350 — Pico 2)

- **MCU:** Raspberry Pi Pico 2 (RP2350, Cortex-M33 Dual-Core con FPU fpv5-sp-d16).
- **Frecuencia del Sistema:** **196 608 000 Hz (196.608 MHz)** — leído de `platformio.ini:5` (`board_build.f_cpu = 196608000L`). Overclock matemático (256 × 768 kHz) que divide limpio a MCLK/SCLK. **NO asumir 125 MHz ni copiar de otro proyecto.**
- **I2S MCLK:** **12.288 MHz** (256 × 48 kHz), generado por `clk_gpout0` y ruteado a `GPIO21` (`main.cpp:131-136`). Pin declarado como `GPIO_FUNC_GPCK`.
- **Sample Rate:** **48 000 Hz** — SSOT única y explícita en `src/config/HardwareConfig.h:24` (`constexpr float SAMPLE_RATE = 48000.0f;`) con alias inmutable `globalSampleRate` (`HardwareConfig.h:16`, `main.cpp:20`). **Deuda SSOT: RESUELTA (D2)** — cero magic numbers repartidos.
- **Watchdog / Thermal (umbrales reales del código):**
  - **Watchdog Timer:** habilitado a **8 000 ms** (`main.cpp:126`) — ampliado desde 1 s para margen a 196 MHz bajo estrés prolongado.
  - **Heartbeat dual-core:** variable atómica `std::atomic<bool> dsp_alive` (`main.cpp:33`). Core 1 hace `dsp_alive.store(true)` en `loop1()` (`main.cpp:269`); Core 0 verifica con `dsp_alive.exchange(false)` en `loop()` (`main.cpp:168`).
  - **Térmico:** lectura vía canal ADC interno `ADC_CHANNEL_TEMP = 4` (`HardwareConfig.h:55`) cada 5 s (`main.cpp:227`):
    - **85 °C** → mute preventivo 100 ms + `FaultLatch::THERMAL_THROTTLE` (`main.cpp:240-245`). Cumple estándar Touring Grade.
    - **95 °C** → `FaultLatch::THERMAL_CRITICAL` + Safe Bypass + Halt (`main.cpp:231-237`). Riesgo físico → sistema detenido.
  - **Sin under-clocking dinámico** (desestabiliza el audio). ✅

---

## 3. Seguridad Física y Protecciones (P1 — innegociables)

- **Relé de Mute (`PIN_MUTE_RELAY = GPIO23`):** corta mecánicamente la salida analógica durante el arranque.
  - Secuencia de power-on (`main.cpp:110-117`): Mute Relay → `LOW` → `delay(500)` → `HIGH` (libera audio).
  - **Topología:** relé **Normally Closed (NC) a tierra** — en ausencia de alimentación o durante boot, la salida está derivada a GND (silencio seguro). Requiere activación explícita (`HIGH`) para abrir el path. (Pendiente confirmar NC-a-tierra en Fase 2: el pin controla la bobina, el contacto NC debe rutearse a GND en el PCB.)
- **Brown-Out Detector (BOD) / VREG:** `vreg_set_voltage(VREG_VOLTAGE_1_10)` (`main.cpp:106`) — tensión de regulador subida a 1.10 V para sostener 196 MHz OC sin corrupción de memoria/RAM en caídas de red eléctrica.
- **Modo Standalone / Redundancia (Safe Bypass):**
  - **Safe Bypass físico** vía **HEF4053BT** (switch analógico CMOS dual) controlado por `PIN_BYPASS_CTRL = GPIO24` (`HardwareConfig.h:56`).
  - Polaridad (`main.cpp:119-122`): `HIGH = bypass ON` (audio entrada→salida directo, **sin DSP**); `LOW = bypass OFF` (audio por DSP).
  - **Comportamiento en fallo:**
    - DSP caído >3 s tras estar armado → bypass ON automático (`main.cpp:174-178`). **El audio nunca se detiene en un show** (regla 5 CLAUDE.md).
    - 95 °C térmico → bypass ON + halt (`main.cpp:233`).
  - **Deuda P1: RESUELTA (D4)** — Safe Bypass físico presente y cableado en código.
- **Stack MSPLIM (anti stack-overflow):** el SDK RP2350 activa **MSPLIM** (Main Stack Pointer Limit, ARMv8-M nativo del Cortex-M33) en `runtime_init_stack_guard.c` al arrancar — cero configuración manual, cero overhead. Si el Stack Pointer baja del límite, el hardware dispara **HardFault** (no hay polling ni canarios de software), interceptado por `isr_hardfault()` (`main.cpp:87-91`): `emergencyMute()` + `watchdog_reboot()`, **sin** `Serial` (BUG FIX #4: en pila corrupta, cualquier llamada a función causa HardFault en cascada).
- **Emergency Mute pre-reset:** fuerza `PIN_I2S_BCLK` y `PIN_I2S_DOUT` a `LOW` antes de cualquier `watchdog_reboot` para evitar ráfagas espurias en el DAC durante la transición (`main.cpp:54-64`).
- **FaultLatch persistente:** `globalFaults.registerFault()` (`src/system/FaultLatch.h`) registra STACK_OVERFLOW, THERMAL_CRITICAL, THERMAL_THROTTLE para diagnóstico post-reset.

---

## 4. Interfaz de Usuario (UX)

- **Display OLED 128×64 (SSD1306, I2C):** `PIN_OLED_SDA = GPIO0`, `PIN_OLED_SCL = GPIO1` (`platformio.ini`). Driver Adafruit SSD1306 + GFX. Refresco **30 fps** con **Dirty Flags por zona** (header/body/footer) — solo repinta lo que cambió (`UIManager.h:52-60`). Jerarquía de menús (`MenuState`, `UIManager.h:11-19`): HOME → EFFECT_SELECT → PARAMETER_EDIT → CALIBRATION → TUNER → **GIG_VIEW** → SAVE_SCREEN.
- **Encoders rotatorios (3 + 1 paralelo):**
  - ENC1: A=GPIO2, B=GPIO3, SW=GPIO4.
  - ENC2: A=GPIO5, B=GPIO6, SW=GPIO7.
  - ENC3: A=GPIO8, B=GPIO9, SW=GPIO10.
  - ENC_PARA (master paralelo): A=GPIO18, B=GPIO19, SW=GPIO20.
  - **Hardware debouncing** RC + Schmitt recomendado en Fase 2 (regla 7.6).
- **Footswitches (4):** `PIN_FS1=11`, `PIN_FS2=12`, `PIN_FS3=13`, `PIN_FS4=22`. Scenes/Snapshots gapless, presets, bypass por canal (`UIManager.h:94-98`).
- **NeoPixels RGB (`PIN_NEOPIXEL = GPIO28`):** indicación de escena/preset, Category Color Coding. `updateNeoPixels()` (`UIManager.h:101`). **TODO menor:** integrar LED rojo parpadeante para faults térmicos (`main.cpp:245`).
- **Master Volume (`PIN_MASTER_VOL = GPIO26`, ADC0):** potenciómetro lee el volumen maestro.
- **Pedal de Expresión:** ver §1.1 (GPIO27).
- **Entropía física (TRNG):** `PIN_ENTROPY_ADC = GPIO29` flotante captura ruido EMF para sembrar PRNG de dither TPDF (`HardwareConfig.h:49`).

---

## 5. Fuente de Alimentación (F.A.) y Gestión de Energía

- **Entrada Principal:** **12 V DC centro negativo** (estándar de pedal Boss/compatible con daisy-chain) + USB-C (datos/power de desarrollo). Diodo de protección de polaridad + fusible PTC reseteable en la entrada DC.
- **Filtrado y Protección:**
  - **EMI/RFI:** ferritas de beads en entrada DC y en líneas sensibles; condensadores X7R de desacoplo 100 nF en cada Vcc de opamp + 10 µF tantalio por etapa.
  - **TVS por línea:** P6KE18CA (entrada instrumento), **P6KE15CA** (líneas de 12 V pre-regulador — ECO 12 V: la P6KE12CA previa, BV 11.4 V, clampea por debajo del riel nominal), CDSOD323-T05LC low-cap en líneas de datos críticas (regla 7.5).
- **Reguladores (topología simétrica para opamps):**
  - **+12 V → −12 V (rail analógico):** inversor conmutado **LT1054** (pin Boost puenteado → oscila >35 kHz inaudible) para generar el rail negativo desde +12 V (alimenta opamps bipolares TL072/NE5532/NJM4556A). **Decisión de arquitectura: LT1054 sobre ICL7660S** por (a) mayor corriente entregada (~100 mA vs ~40 mA) → margen para NE5532 + NJM4556A + TL072 cargados simultáneamente; (b) robustez de gira; (c) a 12 V reales es la **única opción segura**: ICL7660S tiene Absolute Max 12 V (sin margen — explota en el riel nominal), mientras que LT1054 (Absolute Max 15 V) opera con margen correcto (`rules/13 §13.2/§13.3`). Macro SPICE TI SLRS031 validada en el repo. **(ECO 12 V: entrada DC subió de 9 V a 12 V; rales analógicos ahora ±12 V — TL072/NE5532/NJM4556A toleran ±18 V, headroom OK.)**
  - **12 V → 3.3 V (digital/analógico aislado):** Buck → **AP2112K-3.3** (LDO Audio-Grade aislado, regla 9). Separación física entre **3.3 V Digital** (RP2350, OLED, NeoPixels) y **3.3 V Analógico** (VREF ADC, parte analógica del codec).
  - **VBUS 5 V (USB):** protegido contra reversal.
- **Esquema de Tierras (regla 7.10/7.33):** **Plano continuo + partición geométrica** + unión **ÚNICA** entre AGND/DGND/CHASIS en el conector DC. **PROHIBIDA la "Tierra en Estrella"** (antena de bucle con MCLK de MHz). El MCLK a 12.288 MHz hace crítica esta regla.

---

## 6. Auditoría Anti-Colisión GPIO y Deudas Técnicas

### 6.1 Conflictos de pines físicos — **NINGUNO**
Pines usados: GPIO 0-13 (UI), 14-17 (I2S audio), 18-20 (ENC_PARA), 21 (MCLK), 22 (FS4), 23 (Mute Relay), 24 (Bypass), 26-27 (ADC vol/expr), 28 (NeoPixel), 29 (Entropía). **GPIO 25 libre.** Sin duplicaciones.

### 6.2 Dependencias ocultas — **satisfechas**
- **LRCLK = BCLK + 1:** `PIN_I2S_BCLK=14`, `PIN_I2S_LRCLK=15` → cumple la regla automática del bus I2S Pico (regla 3). ✅
- **MCLK derivado:** `clk_gpout0` desde PLL_SYS → `GPIO21` como `GPIO_FUNC_GPCK` (`main.cpp:131-136`). Pinout saneado (no choca con FS4=22). ✅

### 6.3 Canales ADC internos vs GPIO físicos — **naming saneado**
- `ADC_CHANNEL_TEMP = 4` (`HardwareConfig.h:55`) es **canal ADC interno del sensor de temperatura**, no GPIO. Se llama "canal 4" nominalmente, lo que coincide con `GPIO4 = ENC1_SW`. **Deuda D5 RESUELTA** mediante rename (`ADC_CHANNEL_TEMP` en vez de `PIN_TEMP_ADC`) + comentario explícito.

### 6.4 Tabla de deudas técnicas

| ID | Deuda | Evidencia (`archivo:línea`) | Acción correctiva | Estado |
|:--:|---|---|---|:--:|
| D2 | Sample Rate sin SSOT | `HardwareConfig.h:16,24` · `main.cpp:20` | `constexpr` + alias inmutable `globalSampleRate` | ✅ Resuelta |
| D4 | Safe Bypass físico ausente | `HardwareConfig.h:56` · `main.cpp:121,164-178,233` | HEF4053BT + `PIN_BYPASS_CTRL=24` + lógica dual-core/térmica | ✅ Resuelta |
| D5 | Naming `TEMP_ADC` choca con GPIO4 | `HardwareConfig.h:55` | Rename a `ADC_CHANNEL_TEMP` + comentario "canal, no pin" | ✅ Resuelta |
| T1 | LED rojo de fault térmico sin integrar | `main.cpp:245` (TODO) | Conectar NeoPixel de estado a `FaultLatch` | 🔶 Menor (no bloqueante) |
| —  | Mute Relay NC-a-tierra en PCB | `main.cpp:110-117` (pin controla bobina) | Confirmar contacto NC rutheado a GND en esquemático Fase 2 | 🔶 Pendiente Fase 2 |

**Veredicto:** sin deudas técnicas graves. Las 3 deudas históricas (D2/D4/D5) ya están resueltas en firmware. Solo resta T1 (cosmética NeoPixel) y la confirmación del contacto NC en el PCB (Fase 2).

---

## 7. BOM con ROI Máximo (matriz `rules/7 §7.0.3`)

| Función | Componente | Justificación ROI/YAGNI |
|---|---|---|
| MCU | Raspberry Pi Pico 2 (RP2350) | Dual-core M33+FPU a 196 MHz, 264 KB RAM, $4. Mínimo viable para DSP multi-efecto. |
| ADC audio | **PCM1808** (TI) | 24-bit/96 kHz, I2S, dinámica >100 dB, SNR 103 dB. Mejor ROI que AKM para entrada de guitarra. |
| DAC audio | **PCM5102** (TI) | 24-bit, I2S, ripple-rejection integrado (no necesita LDO extra en Vcc analógico), SNR 112 dB. |
| Preamp entrada (guitarra) | **TL072** (x1 canal) | JFET, Rin 1 TΩ, coloración vintage. Prohibido OPA1612 aquí (es pedal vintage, no interfaz transparente). Regla 7.2. |
| Buffer salida línea | **NE5532** (x1 canal) | Bipolar low-noise, drive ±38 mA para línea. Estándar de salida. |
| Amp auriculares | **NJM4556A** (x1 canal) | ±73 mA high-current para 32 Ω. El NE5532 no llega. |
| Inversor rail negativo | **LT1054** | Genera −12 V desde +12 V para opamps bipolares. Elegido sobre ICL7660S por corriente (~100 mA) + robustez de gira + a 12 V es la única opción segura (ICL7660S AbsMax 12 V, ver §5). Macro SPICE TI SLRS031 — pinout en `rules/13 §13.3`. |
| LDO 3.3 V aislado | **AP2112K-3.3** | Audio-grade, aísla 3.3 V digital de analógico. Regla 9. |
| Switch analógico bypass | **HEF4053BT** | Dual SPDT CMOS, low-distortion, controlado por GPIO24 para Safe Bypass. |
| Display | OLED SSD1306 128×64 (I2C) | Bajo consumo, legible en escena, lib Adafruit probada. |
| Encoders x4 | EC11 con detente + switch | Estándar robusto, debouncing HW RC+Schmitt. |
| Footswitches x4 | **SPST momentary controlados por MCU** + **HEF4053BT** | Anti-pop (cero clicks mecánicos del 3PDT al conmutar), más baratos, integrados con scenes/snapshots gapless del `UIManager`. El routing analógico real de bypass lo hace el HEF4053BT (Safe Bypass, §3), no el footswitch. **Decisión: SPST + HEF4053 sobre 3PDT.** |
| Indicador RGB | NeoPixel WS2812 (pin 28) | 1 pin para N LEDs, Category Color Coding. |
| TVS entrada instrumento | **P6KE18CA** | BV 17.1 V bidireccional. Protege pastillas + preamp. |
| TVS línea 12 V | **P6KE15CA** | BV 14.4 V. Protege rail 12 V pre-regulador. (ECO 12 V: la P6KE12CA previa clampea por debajo del riel nominal.) |
| TVS datos USB | **CDSOD323-T05LC** | Low-cap 2.5 pF (no degrada USB high-speed). |
| Diodos señal | **1N4148** | Switching general (debounce, clamp). |
| Acople DC entrada | **10 µF ‖ 100 nF** (electrolítico ‖ C0G/NP0) | Wideband: 10 µF cubre graves, 100 nF cubre transitorios rápidos. |
| Cap anti-aliasing | **C0G/NP0 1 nF** ante PCM1808 | Bajoloss, estable, para filtro reconstrucción. |
| Reguladores ajustables | LM317/LM337 (rail simétrico fino) | Si se requiere ajuste de V del rail opamp. |

---

## 8. Troubleshooting Shield (Síntoma → Causa Raíz) — `rules/7 §7.16`

| Síntoma | Causa Raíz probable | Acción rápida |
|---|---|---|
| **Sin audio** | Mute Relay en LOW (boot colgado) o Safe Bypass atascado ON | Revisar `dsp_alive` (Core 1 vivo); medir `PIN_MUTE_RELAY` y `PIN_BYPASS_CTRL`. |
| **Zumbido 50/60 Hz o 10 kHz** | Tierra en estrella (antena) o loop de masa con otro pedal | Confirmar plano continuo + partición; romper loop con iso-transformer en loop externo. |
| **Sonido apagado / pérdida de agudos** | Cap de acople DC degradado o anti-aliasing abierto | Reemplazar 10 µF input; verificar C0G/NP0 1 nF en PCM1808. |
| **Distorsión a alto nivel (no musical)** | Gain Staging roto: pico pastilla satura ADC | Verificar divisor 15 k/4.7 k → Vpp < 3.0 en ADC; reducir gain TL072. |
| **Pops/clicks en arranque** | Mute Relay no NC-a-tierra o delay(500) omitido | Confirmar contacto NC a GND; validar secuencia `main.cpp:110-117`. |
| **NE5532/NJM4556A quemado** | Rail negativo ausente (LT1054 muerto) → opamp satura un polo | Medir ±9 V; reemplazar inversor conmutado LT1054. |
| **Falsos triggers de footswitch** | Debouncing HW ausente o encoder ruidoso | Añadir RC + Schmitt (regla 7.6); subir tiempo de debounce en firmware. |
| **Audio se corta a los N minutos** | Térmico 85 °C disparando mute preventivo | Revisar ventilación; medir temp vía `DSPMetrics::printReport()`; limpiar disipador. |
| **DSP crashea, audio sigue** | Safe Bypass activado por watchdog | Interpretar como **comportamiento correcto** (regla 5). Revisar `FaultLatch` post-reset. |
| **Jitter / inestabilidad de tono** | MCLK 12.288 MHz corrupto o BCLK/LRCLK mal rutheados | Verificar `clk_gpout0` en GPIO21; confirmar LRCLK=BCLK+1. |

---

## ✅ Auto-Check (Cero Alucinación) — Prompt 1

- [x] **Nombre del proyecto** coincide con el código (`1_Pedal_DSP_GTR`).
- [x] **Frecuencia MCU** leída de `platformio.ini` (196 608 000 Hz, no asumida).
- [x] **Sample Rate SSOT** explícita (`HardwareConfig.h:24`); deuda D2 resuelta.
- [x] **Deudas RT-Safety reportadas**: umbrales térmicos **reales** 85/95 °C (`main.cpp:231,240`); **Safe Bypass físico** presente (`PIN_BYPASS_CTRL`).
- [x] **Sin phantom fantasma**: pedal de guitarra, no mic condensador (sin +48 V).
- [x] **TL072** especificado para entrada vintage (NO OPA1612 — no es interfaz transparente).
- [x] **Gain Staging Thévenin** documentado (divisor 15 k/4.7 k, carga 20 kΩ → 1.85 Vpp < 3.0 Vpp).
- [x] **Acople DC wideband** (10 µF ‖ 100 nF) detallado en §1.1.
- [x] **Troubleshooting Shield** incluido (§8).
- [x] **TVS exactos** por tipo de línea (P6KE18CA/P6KE12CA/CDSOD323-T05LC) + anti-aliasing **C0G/NP0**.
- [x] **Relé de Mute NC-a-tierra** aclarado (§3, pendiente confirmar contacto en Fase 2).
- [x] **Sin periféricos inventados**: extraídos de `platformio.ini`, `src/ui/UIManager.h`, `src/audio/AudioSystemI2S.h`, includes de `main.cpp`.
- [x] **Tierra = plano continuo + partición geométrica** (NO estrella).
- [x] **Sin magic numbers**: cada cifra con fuente (`platformio.ini:5`, `main.cpp:126`, `rules/7.x`).

---

## Próximo paso

Este documento es la **entrada literal** del Prompt 2 (`tools/Prompts/2_prompt_generador_arquitectura_hardware.md` v1.4), que generará `docs/Arquitectura_Hardware.md` con el **ruteo pin-a-pin** (SKiDL-ready), matriz de nodos, y cálculos de filtros exactos.

### Decisiones de arquitectura RESUELTAS (v1.1 — 21 Julio 2026)

Las 3 decisiones abiertas de la v1.0 están cerradas y retro-propagadas a este spec:

| # | Decisión | Resolución | Reflejada en |
|:--:|---|---|---|
| 1 | Inversor del rail negativo | **LT1054** (sobre ICL7660S) | §5, §7 BOM, §8 |
| 2 | Footswitches | **SPST momentary + HEF4053BT** (sobre 3PDT) | §7 BOM (+ §3 Safe Bypass) |
| 3 | Salida principal | **Estéreo L/R** (sobre mono) | §1.2 (coherente con `AudioSystemI2S.cpp`) |

**Listo para Fase 2.** El Prompt 2 v1.4 ya **no debe pausar** por estas 3 decisiones (cláusula "Decisiones Abiertas" regla 12) — están explícitamente cerradas aquí y citadas con `<spec:§>`. Cualquier **nueva** ambigüedad que el Prompt 2 detecte al expandir SÍ debe pausar.
