# Arquitectura de Hardware: 1_Pedal_DSP_GTR

> **Versión:** 1.2 (Fase 2 — generada por Prompt 2 v1.5 · canon Hardware-As-Code v1.3)
> **Fix v1.1:** Cierre de 🟥 DRC Fase 4 — **Regla 13 ESD/RFI universal en conectores** aplicada (TVS P6KE12CA + ferrite BLM18 + C 47 pF C0G/NP0 al CHASIS en OUT-L/OUT-R/HP, post-R-iso pre-jack); completada dupla TVS+C47pF en entrada; NeoPixel ×N → ×8 (DRC entero).
> **Fix v1.2 (ECO 12 V — Prompt 5 DRC Paridad SSOT):** el `SPICE_Report.md` midió el riel del LT1054 en **−12.000 V**; el SSOT declaraba **9 V** (`VEE_9V`/`VCC_12V`, jack DC 9 V, BOM U4, TVS pre-regulador P6KE12CA). El pedal trabaja con **12 V DC centro negativo** (confirmación de campo). Se renombran los rieles `VEE_9V`→`VEE_12V`, `VCC_12V`→`VCC_12V`, se eleva el jack DC a 12 V, y **la TVS pre-regulador pasa a P6KE15CA** (BV 14.4 V) — la P6KE12CA previa (BV 11.4 V) clampea 0.6 V *por debajo* del riel de 12 V y **no protege**. La nota "futuro 12 V" del LT1054 queda obsoleta: el 12 V es el presente. Primer ECO real aplicado por el Prompt 5 (dogfooding del Nivel 5 de la cadena anti-deuda).
> **Fecha:** 21 Julio 2026
> **Target:** Raspberry Pi Pico 2 (RP2350, Cortex-M33 Dual-Core, FPU fpv5-sp-d16)
> **Dominio:** 🎸 **Touring Grade** (pedal multi-efectos de guitarra en vivo)
> **Fuentes trazables:** `HARDWARE_SPEC.md v1.1` · `rules/7_diseno_electronico.md` · `rules/13_pinouts_canonicos.md` · `src/config/HardwareConfig.h` · `src/audio/AudioSystemI2S.cpp` · `src/ui/UIManager.h` · `src/main.cpp` · `platformio.ini`

Este documento detalla el ruteo electrónico y las topologías de conexión del circuito. Es la **Fuente de Verdad** para la generación de la Netlist (SKiDL) por la Fase 4 (Prompt 3). Todo ruteo se expresa en formato literal `Componente A · Pin X → Componente B · Pin Y`.

**Decisiones de arquitectura cerradas (spec v1.1):** (1) Inversor **LT1054** (no ICL7660S); (2) Footswitches **SPST + HEF4053BT** (no 3PDT); (3) Salida **estéreo L/R** (no mono). Ya no son decisiones abiertas para este documento.

---

## 1. Topología Analógica y Codecs

### 1.1 Cadena de Entrada — Guitarra → ADC PCM1808 (CANAL L+R estéreo)

> Topología: pastilla → protección TVS + ferrite → acople DC wideband → divisor Thévenin (anti-aliasing + gain staging) → TL072 (preamp JFET) → red RC VREF/VCOM del ADC → PCM1808. **Estéreo L+R** (2 jacks de entrada, uno por canal del TL072 dual).

```
Jack IN-L · TIP ─────────────────────────────────────────────────────────────────────►
            │
            ├─ PTC rearmable 500 mA ── (protección de entrada, regla 7.31)
            ├─ Ferrite bead BLM18 ── (anti-RFI, regla 7.18)
            ├─ TVS P6KE18CA · A ── Jack IN-L · TIP
            ├─ TVS P6KE18CA · K ── CHASIS GND          (barrera TVS al CHASIS, no audio GND — regla 7.17)
            └─ C 47 pF C0G/NP0 ── Jack IN-L · TIP → CHASIS GND   (anti-EMI RF, cierra dupla TVS+C47pF — Regla 13, rules/7 §7.17)

Jack IN-L · TIP ─┬─ C_in1A 10 µF electrolítico · +  ─┐  (acople DC wideband, regla 7.11)
                 └─ C_in1B 100 nF C0G/NP0 · .        ┘  ‖ en paralelo → nodo IN_L_AC

IN_L_AC ── R1 15 kΩ ── (R2 4.7 kΩ ── AGND) ── nodo DIV_L     (divisor Thévenin, regla 7.0.1)
                          │
                          └─ C_aalias 1 nF C0G/NP0 ── AGND   (condensador anti-aliasing — regla 7.5, C0G/NP0 obligatorio)

DIV_L ── TL072 · 3 (IN_A+)                                (TL072 pinout DIP-8 — rules/13 §13.1)
TL072 · 2 (IN_A−) ── R_fb 470 kΩ ── TL072 · 1 (OUT_A)     (realimentación, gain ×5 aprox. pastilla humbucker hot)
TL072 · 4 (V−) ── VEE_12V (rail −12 V del LT1054)
TL072 · 8 (V+) ── VCC_12V (rail +12 V)

TL072 · 1 (OUT_A) ── C_block_ADC 10 µF ── PCM1808 · 4 (VINL)   (acople AC al midrail +2.5 V del ADC — sin él, clipping asimétrico — rules/13 §13.8 Ley 9)
```

La cadena **CANAL R** es idéntica usando la **mitad B del TL072** (`TL072 · 5 IN_B+`, `TL072 · 6 IN_B−`, `TL072 · 7 OUT_B`) → `PCM1808 · 5 (VINR)`. Confirmación estéreo: el codec y el firmware ya procesan L+R (`AudioSystemI2S.cpp:111 read24(&inLeft,&inRight)`).

**Gain Staging Thévenin (verificado, spec §1.1):** divisor 15 kΩ / 4.7 kΩ con carga 20 kΩ del ADC → $V_{ADC} \approx V_{in} \cdot 0.201$. Para $V_{in}=9.2$ Vpp (humbucker hot ×5) → $V_{ADC} \approx 1.85$ Vpp **< 3.0 Vpp** (full-scale PCM1808) → ✅ 6 dB headroom, sin saturación.

### 1.2 Codecs — Configuración estática pin-a-pin (rules/13 §13.8/§13.9)

**ADC PCM1808 (TI SBAS099, TSSOP-16) — I2S Slave, requiere MCLK externo:**

| PCM1808 Pin | Conexión | Origen |
|:---:|---|---|
| `1` VREF | Red RC: 10 µF ‖ 100 nF ── AGND | rules/13 §13.8 Ley 9 (innegociable) |
| `2` VCOM | Red RC: 10 µF ‖ 100 nF ── AGND | rules/13 §13.8 Ley 9 |
| `3` AGND | AGND | — |
| `4` VINL | ← TL072 · 1 (OUT_A) vía C_block_ADC | §1.1 |
| `5` VINR | ← TL072 · 7 (OUT_B) vía C_block_ADC | §1.1 (canal R) |
| `6` VCC | +5 V (rango 4.5–5.5 V; rail analógico) | — |
| `7` FMT | Strap a DGND (I2S mode) | SBAS099 tabla esclavo |
| `8` LRCK | ← RP2350 · GPIO15 (PIN_I2S_LRCLK) | HardwareConfig.h:33 |
| `9` DOUT | → RP2350 · GPIO17 (PIN_I2S_DIN) | HardwareConfig.h:37 |
| `10` BCK | ← RP2350 · GPIO14 (PIN_I2S_BCLK) | HardwareConfig.h:29 |
| `11` SCKI (MCLK) | ← RP2350 · GPIO21 (clk_gpout0, 12.288 MHz) | main.cpp:131-136 |
| `12` MD0 | Strap a DGND (slave) | SBAS099 |
| `13` MD1 | Strap a DGND (slave) | SBAS099 |
| `14` DGND | DGND | — |
| `15` VDD | +3.3 V (desde **AP2112K aislado**, no del MCU) | §3 regla 2 |
| `16` PD | → +3.3 V (operación, NO power-down) | — |

> 🟨 `[VERIFICAR DS SBAS099 antes de crear símbolo KiCad custom]` — pinout funcional confirmado; el mapeo físico completo de TSSOP-16 debe contrastarse contra la tabla de pin assignments del PDF al crear el símbolo (deuda `wiring_logic.py:104-109`).

**DAC PCM5102A (TI SLES025/SLOS601, TSSOP-20) — PLL interno (NO requiere MCLK):**

| PCM5102A Pin | Conexión | Origen |
|:---:|---|---|
| `1` CAPP | C_chgpump 2.2 µF X7R · pin 1 | rules/13 §13.9 |
| `2` CAPM | C_chgpump 2.2 µF X7R · pin 2 | rules/13 §13.9 |
| `3` VNEG | (riel negativo generado interno — NO conectar) | — |
| `4` AGND | AGND | — |
| `5` AVDD | +3.3 V (desde **AP2112K aislado**) | §3 regla 2 |
| `11` SCL | ── GND (PLL interno self-clocking — **NO** MCLK externo) | rules/13 §13.9 Ley 10 |
| `13` BCK | ← RP2350 · GPIO14 (PIN_I2S_BCLK) | HardwareConfig.h:29 |
| `14` DIN | ← RP2350 · GPIO16 (PIN_I2S_DOUT) | HardwareConfig.h:41 |
| `15` LRCK | ← RP2350 · GPIO15 (PIN_I2S_LRCLK) | HardwareConfig.h:33 |
| `17` XSMT | → **+3.3 V fijo** (NO flotante, NO GND — sin esto NO hay sonido) | rules/13 §13.9 Ley 10 |

> Las 3 invariantes Touring Grade del PCM5102A (rules/13 §13.9): (1) **XSMT a 3.3 V**; (2) **SCL a GND** (no MCLK externo); (3) **AVDD/CPVDD a 3.3 V** (chip suelto) o **5 V** (módulo WCMRU genérico). Este proyecto usa **chip suelto a 3.3 V desde AP2112K**.
>
> 🟨 `[VERIFICAR DS SLES025 antes de crear símbolo KiCad custom]` — pines funcionales confirmados; el mapeo físico completo de TSSOP-20 (VOUTL/VOUTR, DSN/DSP, DEMP, FMT, MODE, ZERO, DVDD, CPVDD) debe contrastarse contra el PDF.

### 1.3 Cadena de Salida — DAC PCM5102A → Salidas (ESTÉREO + Auriculares)

> Topología: PCM5102A VOUTL/VOUTR → NE5532 dual (buffer L+R, salida principal estéreo) + NJM4556A (auriculares). Mute relay G5V-1 en serie con la salida principal. Safe Bypass HEF4053BT puentea IN→OUT cuando el DSP cae.

```
PCM5102A · VOUTL  ── C_out_L 10 µF ── NE5532 · 3 (IN_A+)            (rules/13 §13.1 pinout DIP-8)
NE5532 · 2 (IN_A−) ── NE5532 · 1 (OUT_A)                            (buffer unidad, gain=1)
NE5532 · 4 (V−) ── VEE_12V ; NE5532 · 8 (V+) ── VCC_12V
NE5532 · 1 (OUT_A) ── R_iso 100 Ω ── G5V-1 · 5 (COM) ── G5V-1 · 6 (COM)
G5V-1 · 10 (NO) ── nodo OUT_L_JACK                                   (mute: contacto NC a GND, ver §1.4)
OUT_L_JACK ── Ferrite bead BLM18 ── Jack OUT-L · TIP                 (anti-RFI, regla 7.18)
OUT_L_JACK ── TVS P6KE12CA · A ; TVS · K ── CHASIS GND               (Regla 13 — salida no balanceada TAMBIÉN lleva TVS, rules/7 §7.17)
OUT_L_JACK ── C 47 pF C0G/NP0 → CHASIS GND                           (anti-EMI RF, dupla Regla 13)

PCM5102A · VOUTR  ── C_out_R 10 µF ── NE5532 · 5 (IN_B+)            (mitad B del dual → canal R)
NE5532 · 6 (IN_B−) ── NE5532 · 7 (OUT_B)
NE5532 · 7 (OUT_B) ── R_iso 100 Ω ── G5V-1 · [COM del 2º relé R] ── nodo OUT_R_JACK
OUT_R_JACK ── Ferrite bead BLM18 ── Jack OUT-R · TIP                 (anti-RFI)
OUT_R_JACK ── TVS P6KE12CA · A ; TVS · K ── CHASIS GND               (Regla 13)
OUT_R_JACK ── C 47 pF C0G/NP0 → CHASIS GND                           (dupla Regla 13)
```

**Auriculares (downmix L+R → monoaural para driver high-current):**
```
NE5532 · 1 (OUT_A) ── R_mix 10 kΩ ─┐
NE5532 · 7 (OUT_B) ── R_mix 10 kΩ ─┴─ nodo HP_MIX                (downmix pasivo L+R)
HP_MIX ── C_hp 10 µF ── NJM4556A · 3 (IN_A+)                      (rules/13 §13.1; NJM4556A es DIP-8 JEDEC)
NJM4556A · 2 (IN_A−) ── NJM4556A · 1 (OUT_A) ── R_iso_HP 22 Ω ── nodo HP_JACK   (R-iso baja para carga 32 Ω)
HP_JACK ── Ferrite bead BLM18 ── Jack HP-L · TIP
HP_JACK ── TVS P6KE12CA · A ; TVS · K ── CHASIS GND                   (Regla 13)
HP_JACK ── C 47 pF C0G/NP0 → CHASIS GND                               (dupla Regla 13)
NJM4556A · 4 (V−) ── VEE_12V ; NJM4556A · 8 (V+) ── VCC_12V
```

> **Por qué NJM4556A y no NE5532 para auriculares:** el NE5532 (±38 mA) no drivea cargas de 32 Ω con headroom; el NJM4556A (±73 mA) sí (spec §1.2). Macro SPICE `spice_models/NJM4556A.lib` con ilimit=73 mA.

> **Anti-Phantom OMITIDO (KISS/YAGNI), TVS NO omitido (Regla 13):** la salida es TS/TRS **no balanceada** (pedal, no XLR), así que no hay +48 V que retornar → se omite la red anti-phantom (C 47 µF 63 V bipolar + R 100 Ω, regla 8 condicional). **PERO** el blindaje ESD/RFI del conector **NO se omite** (Regla 13, ortogonal a la ruta de señal): cada jack externo (IN/OUT/HP) lleva TVS bidireccional P6KE12CA/18CA + ferrite BLM18 + C 47 pF C0G/NP0 al **CHASIS GND**, **post-R-iso pre-jack** (rules/7 §7.17 Principio Cero + §7.18 Jaula de Faraday).

### 1.4 Mute Relay + Safe Bypass (P1 Safety — el audio nunca se detiene)

**Mute Relay G5V-1 (Omron, pinout rules/13 §13.4):**
```
RP2350 · GPIO23 (PIN_MUTE_RELAY) ── R_base 1 kΩ ── 2N3904 · 2 (B)     (drive por transistor, NO GPIO directo)
2N3904 · 1 (E) ── GND ; 2N3904 · 3 (C) ── G5V-1 · 9 (Coil−)
G5V-1 · 2 (Coil+) ── +5 V                                            (rules/13 §13.4 Ley 5)
D1 1N4148 · K ── G5V-1 · 2 (Coil+) ; D1 · A ── G5V-1 · 9 (Coil−)     (diodo flyback — innegociable, regla 8)
G5V-1 · 1 (NC sin uso) ── red NC
G5V-1 · 5/6 (COM) ── path de salida (ver §1.3)
G5V-1 · 10 (NO) ── Jack OUT · TIP
```
> **Topología Touring Grade:** contacto **NC a tierra** — en ausencia de VCC o durante boot, la salida está derivada a GND (silencio seguro). Requiere `HIGH` explícito del firmware (`main.cpp:110-117`) para abrir el path. Secuencia boot: `MUTE=LOW → delay(500) → MUTE=HIGH`. **Confirmación PCB Fase 2:** el contacto NC debe rutearse físicamente a GND (deuda del spec §6.4).

**Safe Bypass HEF4053BT (Nexperia SOT109-1, pinout rules/13 §13.5):**
```
RP2350 · GPIO24 (PIN_BYPASS_CTRL) ── R_pulldown 100 kΩ ── GND        (FAIL-SAFE: al perder VCC cae a LOW = bypass ON)
RP2350 · GPIO24 ── HEF4053BT · 10 (S1/nA, select switch 1)
HEF4053BT · 11 (Z1, común switch 1) ── nodo OUT_POST_DAC              (salida DSP)
HEF4053BT · 12 (Y11, canal HIGH) ── nodo IN_RAW                       (entrada directa = Dry Through)
HEF4053BT · 13 (Y10, canal LOW) ── red NC                             (canal sin uso)
HEF4053BT · 1 (S3), · 5 (Z2), · 4 (Z3) ── red NC                     (switches 2 y 3 sin uso)
HEF4053BT · 9 (S2/nB), · 15 (E/nE) ── GND                            (E=LOW habilita switches)
HEF4053BT · 16 (VDD) ── +3.3 V ; · 8 (VSS) ── GND ; · 14 (VEE) ── GND (VEE↔VSS, single-supply)
```
> **Ley 6 HEF4053 Safe Bypass (rules/13 §13.5):** se usa **1 solo de los 3 switches** (switch 1). Control S1 con **pull-down** → al perder VCC cae a LOW = Y10 (Dry Through) seleccionado. Polaridad firmware (`main.cpp:119-122`): `HIGH = bypass ON`, `LOW = bypass OFF`. Comportamiento en fallo: DSP caído >3 s → bypass ON automático (`main.cpp:174-178`); 95 °C → bypass ON + halt (`main.cpp:233`).
>
> 🟨 `[VERIFICAR DS Nexperia HEF4053B]` — los pines funcionales están confirmados; el mapeo físico exacto de canales Y1x/Y2x/Y3x entre Nexperia y TI CD4053B tiene permutaciones documentadas. Contrastar al crear símbolo KiCad custom.

---

## 2. Microcontrolador, GPIOs y Relojes

### 2.1 RP2350 + Relojes matemáticos

- **MCU:** Raspberry Pi Pico 2 (RP2350, Cortex-M33 Dual-Core con FPU fpv5-sp-d16, 264 KB RAM). Frecuencia de sistema **196 608 000 Hz** leída de `platformio.ini:5` (`board_build.f_cpu = 196608000L`) — overclock matemático (256 × 768 kHz) que divide limpio a MCLK/SCLK.
- **Sample Rate SSOT:** **48 000 Hz** (`HardwareConfig.h:24 constexpr float SAMPLE_RATE = 48000.0f`, alias inmutable `globalSampleRate`). Deuda D2 RESUELTA.
- **MCLK 12.288 MHz:** generado por `clk_gpout0` del RP2350, ruteado a **GPIO21** como `GPIO_FUNC_GPCK` (`main.cpp:131-136`). 256 × 48 kHz (reloj matemático anti-jitter).
- **BOD/VREG:** `vreg_set_voltage(VREG_VOLTAGE_1_10)` (`main.cpp:106`) — 1.10 V para sostener 196 MHz OC sin corrupción RAM en caídas de red.
- **Dual-core + Watchdog:** Watchdog 8000 ms (`main.cpp:126`); heartbeat atómico `std::atomic<bool> dsp_alive` (Core 1 `store(true)` en `loop1()` `main.cpp:269`; Core 0 `exchange(false)` en `loop()` `main.cpp:168`).
- **Thermal:** canal ADC interno `ADC_CHANNEL_TEMP = 4` (`HardwareConfig.h:55`) cada 5 s → 85 °C mute preventivo 100 ms + `THERMAL_THROTTLE` (`main.cpp:240-245`); 95 °C bypass + halt + `THERMAL_CRITICAL` (`main.cpp:231-237`).

### 2.2 Mapa SSOT de GPIO (leído del firmware — regla 10, cero inventados)

| GPIO | Función | Pin físico Pico 2 | Fuente |
|:---:|---|:---:|---|
| GPIO0 | OLED SDA (I2C) | 1 | platformio.ini |
| GPIO1 | OLED SCL (I2C) | 2 | platformio.ini |
| GPIO2 | ENC1 A | 4 | UIManager.h |
| GPIO3 | ENC1 B | 5 | UIManager.h |
| GPIO4 | ENC1 SW | 6 | UIManager.h |
| GPIO5 | ENC2 A | 7 | UIManager.h |
| GPIO6 | ENC2 B | 9 | UIManager.h |
| GPIO7 | ENC2 SW | 10 | UIManager.h |
| GPIO8 | ENC3 A | 11 | UIManager.h |
| GPIO9 | ENC3 B | 12 | UIManager.h |
| GPIO10 | ENC3 SW | 14 | UIManager.h |
| GPIO11 | FS1 (footswitch) | 15 | UIManager.h:94 |
| GPIO12 | FS2 | 16 | UIManager.h:94 |
| GPIO13 | FS3 | 17 | UIManager.h:94 |
| GPIO14 | **I2S BCLK** | 19 | HardwareConfig.h:29 |
| GPIO15 | **I2S LRCK** (= BCLK+1) | 20 | HardwareConfig.h:33 |
| GPIO16 | **I2S DOUT** (→ DAC) | 21 | HardwareConfig.h:41 |
| GPIO17 | **I2S DIN** (← ADC) | 22 | HardwareConfig.h:37 |
| GPIO18 | ENC_PARA A | 24 | UIManager.h |
| GPIO19 | ENC_PARA B | 25 | UIManager.h |
| GPIO20 | ENC_PARA SW | 26 | UIManager.h |
| GPIO21 | **MCLK** (clk_gpout0) | 27 | main.cpp:131-136 |
| GPIO22 | FS4 | 29 | UIManager.h:94 |
| GPIO23 | **Mute Relay** | 31 | HardwareConfig.h:54 |
| GPIO24 | **Bypass CTRL** (HEF4053) | 32 | HardwareConfig.h:56 |
| GPIO26 | Master Volume (ADC0) | 31 | spec §4 |
| GPIO27 | Expression Pedal (ADC1) | 32 | spec §4 |
| GPIO28 | NeoPixel (WS2812B) | 34 | spec §4 |
| GPIO29 | Entropy ADC (TRNG, flotante) | — | HardwareConfig.h:49 |

> Mapeo GPIO→pin físico THT según `rules/13 §13.11` (diccionario `PICO2_GPIO_TO_PIN`). **GPIO25 libre** (LED on-board del Pico, sin asignar en este proyecto).

### 2.3 Auditoría Anti-Colisión GPIO (regla 9)

- **Sin duplicaciones:** 28 GPIOs usados, todos distintos. GPIO25 libre.
- **LRCLK = BCLK + 1:** GPIO14/GPIO15 → cumple la regla automática del bus I2S Pico (regla 3). ✅
- **MCLK no choca con FS4:** GPIO21 (MCLK) vs GPIO22 (FS4) → pines adyacentes pero distintos. ✅
- **ADC_CHANNEL_TEMP = 4** es canal interno del sensor de temperatura, **NO GPIO4** (GPIO4=ENC1_SW). Deuda D5 RESUELTA vía rename. ✅

---

## 3. Fuente de Alimentación y Aislamiento (F.A.)

> Voltaje de entrada **LEÍDO DEL SPEC**: **12 V DC** centro negativo (estándar pedal Boss/daisy-chain) + USB-C desarrollo. Topología: protección entrada → LT1054 (rail negativo) → Buck 5 V → LDOs 3.3 V (digital + analógico aislado AP2112K).

```
DC Jack 12 V · + ── PTC rearmable 500 mA ── TVS SMAJ15A ── MOSFET-P IRLML6401 ── nodo VCC_12V
                                                                    │
DC Jack 12 V · − ── CHASIS GND                                        (regla 7.31: MOSFET-P da protección contra
                                                                     inversión con caída casi nula y cero calor,
                                                                     protege venga la polaridad de donde venga)

VCC_12V ── LT1054 · 8 (V+)                                            (rules/13 §13.3 pinout DIP-8)
LT1054 · 7 (OSC) ── LT1054 · 8 (V+)  (vía R_boost 20 kΩ, modo BOOST >35 kHz inaudible)
LT1054 · 1 (FB) ── LT1054 · 5 (Vout)  (realimentación para regular)
LT1054 · 2 (C1+) ── C_pump 10 µF · + ── LT1054 · 4 (C1−)            (cap de bombeo)
LT1054 · 3 (GND) ── AGND
LT1054 · 5 (Vout) ── nodo VEE_12V  (rail negativo −12 V)
LT1054 · 6 (Vref) ── red NC                                          (referencia interna 2.5 V, sin uso)

VCC_12V ── Buck Step-Down (5 V) ── nodo VCC_5V                        (alimenta VSYS del Pico + NeoPixels —
                                                                     evita disipación del lineal)
VCC_5V ── LDO lineal 3.3 V (digital) ── nodo VCC_3V3_DIG             (RP2350, OLED, NeoPixels)
VCC_5V ── AP2112K-3.3 (analógico aislado) ── nodo VCC_3V3_ANA        (codecs PCM1808/PCM5102A — regla 2)
   AP2112K · 1 (VIN) ── VCC_5V
   AP2112K · 2 (GND) ── AGND
   AP2112K · 3 (EN) ── VCC_5V  (always-on)
   AP2112K · 4 (BP/NR) ── C_bp 10 nF ── AGND  (baja ruido)
   AP2112K · 5 (VOUT) ── VCC_3V3_ANA ; C_out 4.7 µF tantalio ── AGND (estabilidad LDO)
```

> **PROHIBIDO** que el audio digital se alimente del `3V3(OUT)` ruidoso del RP2350 (regla 2). El AP2112K es **dedicado exclusivo** a codecs + OLED. Macro SPICE AP2112K + LT1054 (TI SLRS031) validadas en el repo.
>
> **Decisión LT1054 sobre ICL7660S:** el LT1054 entrega ~100 mA (vs ~40 mA del ICL7660S) → margen para TL072 + NE5532 + NJM4556A cargados. A **12 V de entrada** es la única opción segura: el ICL7660S tiene Absolute Max 12 V (i.e. sin margen — explota en el riel nominal). El LT1054 (Absolute Max 15 V) opera con margen correcto a 12 V. Detalle en spec §5.

**Esquema de Tierras (regla 7.10/7.33):** **Plano continuo + partición geométrica** + unión **ÚNICA** AGND/DGND/CHASIS en el conector DC. **PROHIBIDA la "Tierra en Estrella"** (antena de bucle con el MCLK de 12.288 MHz). Las ferrite beads separan AGND↔DGND↔CHASIS con un único punto de unión monopunto.

---

## 4. Interfaz de Usuario (Hardware Debouncing)

> Topología anti-rebote: `Señal → Red RC (10 kΩ pull-up + 100 nF a GND) → 74HC14 Schmitt → GPIO` (regla 7, cero debouncing por software).

```
(Para cada encoder A/B y cada footswitch FSx):
Jack/Encoder · señal ── R_pull 10 kΩ ── +3.3 V_DIG                   (pull-up)
                    ── C_deb 100 nF ── GND                            (filtro RC, τ = 1 ms)
                    ── 74HC14 · Ax (entrada inversor)                 (rules/13 §13.7 pinout DIP-14)
74HC14 · Yx (salida Schmitt limpio) ── RP2350 · GPIOx
74HC14 · 14 (VCC) ── VCC_3V3_DIG ; · 7 (GND) ── GND                   (desde rail DIGITAL, no AP2112K)
```

**Asignación de inversores del 74HC14 (hex Schmitt, 6 pares A/Y):**
- ENC1.A → 74HC14 · 1A → · 1Y → GPIO2
- ENC1.B → 74HC14 · 2A → · 2Y → GPIO3
- ENC2.A → 74HC14 · 3A → · 3Y → GPIO5
- ENC2.B → 74HC14 · 4A → · 4Y → GPIO6
- ENC3.A → 74HC14 · 5A → · 5Y → GPIO8
- ENC3.B → 74HC14 · 6A → · 6Y → GPIO9
- (FS1/FS2/FS3/FS4 y ENC*.SW → segundo 74HC14 o debounce por RC directo al GPIO con interruptor Schmitt interno del RP2350 — decisión de Fase 2 según budget de inversores)

**NeoPixels (rules/13 §13.14):**
```
RP2350 · GPIO28 ── R_data 470 Ω ── WS2812B[0] · 4 (DIN)              (rules/13 §13.14, level-shift opcional vía 74AHCT125 si brillo insuficiente)
WS2812B[i] · 2 (DOUT) ── WS2812B[i+1] · 4 (DIN)                      (cascada estrictamente unidireccional)
WS2812B · 1 (VDD) ── VCC_5V + C 100 µF ‖ 100 nF (anti-inrush primer LED)
WS2812B · 3 (VSS) ── GND
```

> **TODO menor (T1):** integrar NeoPixel rojo parpadeante para faults térmicos (`main.cpp:245`).

---

## 5. Metas de Simulación Analógica (Touring Grade — por banco aislado)

> **DOMINIO: Touring Grade (HARDWARE analógico).** Studio Grade (>105 dB) es EXCLUSIVO del DSP/código y NUNCA rige estos umbrales (anti-patrón A11). El MCU digital es un "agujero negro" para SPICE — cada etapa se mide por separado (Stage Isolation).

| Banco | DUT aislado | Análisis | Umbrales Touring Grade |
|---|---|---|---|
| **ENTRADA** | Jack IN → TL072 → PCM1808 VINL | AC sweep 20 Hz–20 kHz | Anti-aliasing C0G/NP0; headroom divisor Thévenin **1.85 Vpp < 3.0 Vpp** |
| **SALIDA L** | PCM5102A VOUTL → NE5532 → Jack OUT-L | Transitorio THD @ 1 kHz | THD+N **< 0.1 %**, SNR **> 95 dB(A)**, Respuesta **±0.5 dB (20 Hz–20 kHz)** |
| **SALIDA R** | PCM5102A VOUTR → NE5532 → Jack OUT-R | Transitorio THD @ 1 kHz | Idem (simetría L/R) |
| **AURICULARES** | NJM4556A → Jack HP | Transitorio THD @ 32 Ω carga | THD+N **< 0.1 %** con carga 32 Ω |
| **ALIMENTACIÓN** | LT1054 + Buck + AP2112K | Transitorio + AC | Ripple LDOs; frecuencia inversor **>35 kHz** (inaudible) |

- **Umbrales Touring Grade (cabecera obligatoria del YAML de Fase 5):** THD+N **< 0.1 % @ 1 kHz**, SNR **> 95 dB(A)**, Respuesta en frecuencia **±0.5 dB (20 Hz–20 kHz)**.
- **Nota HPF:** para filtros paso-altos, especificar atenuación a **0.1 Hz** (no a `cutoff_frequency_hz`) — el motor SPICE genérico solo busca cruces descendentes.
- **Targets de ripple/PSRR de riel:** documentar como `# TODO (gap testbench motor)` — el inyector `vstim` a masa choca con `V_VCC` DC (Prompt 3 D.6).

---

## 6. BOM (SKiDL-ready) — alimenta `inventario_bruto.csv`

| Ref_Local | Valor | Package | Montaje | Veredicto ROI/YAGNI |
|---|---|---|---|---|
| MCU | Raspberry Pi Pico 2 (RP2350) | módulo | THT | Dual-core M33+FPU 196 MHz, 264 KB RAM. Must-have. |
| ADC | PCM1808 (TI) | TSSOP-16 | SMD | 24-bit/96 kHz I2S, SNR 103 dB. Must-have. |
| DAC | PCM5102A (TI) | TSSOP-20 | SMD | 24-bit I2S, ripple-rejection integrado, SNR 112 dB. Must-have. |
| U1 | TL072 (preamp guitarra L+R) | DIP-8 | THT | JFET, Rin 1 TΩ, coloración vintage. Prohibido OPA1612. |
| U2 | NE5532 (salida línea estéreo L+R) | DIP-8 | THT | Bipolar low-noise, drive ±38 mA. Estándar salida. |
| U3 | NJM4556A (auriculares) | DIP-8 | THT | ±73 mA high-current para 32 Ω. El NE5532 no llega. |
| U4 | LT1054 (inversor rail negativo) | DIP-8 | THT | −12 V desde +12 V, ~100 mA, robusto gira. Único válido a 12 V (ICL7660S AbsMax 12 V). |
| U5 | AP2112K-3.3 (LDO audio aislado) | SOT-23-5 | SMD | Aísla 3.3 V digital de analógico (codecs). Regla 2. |
| U6 | 74HC14 (debouncing Schmitt) | DIP-14 | THT | 6 inversores para encoders/footswitches. Cero debounce SW. |
| U7 | 74HC14 (2º, si budget inversores >6) | DIP-14 | THT | Opcional según cuenta de señales a debounzar. |
| U8 | HEF4053BT (Safe Bypass) | SO-16 | SMD | Triple SPDT CMOS, controlado GPIO24. P1 Safety. |
| K1, K2 | G5V-1 (relé mute L y R) | DIP | THT | NC a tierra, drive 2N3904 + flyback. Estéreo = 2 relés. |
| Q1 | 2N3904 (drive relé mute) | TO-92 | THT | NPN, drive bobina relé. |
| Q2 | IRLML6401 (MOSFET-P protección polaridad) | SOT-23 | SMD | Protección inversión DC, caída casi nula. |
| D1, D2 | 1N4148 (flyback relés) | DO-35 | THT | Diodo flyback en bobina de cada G5V-1. |
| LED_RGB | NeoPixel WS2812B (×8) | 5050 | SMD | Category Color Coding, 1 pin GPIO28. Cuenta fija entera (DRC Fase 4). |
| OLED | SSD1306 128×64 (I2C) | módulo | THT | Legible en escena. |
| ENC×4 | EC11 con detente + switch | THT | THT | Encoders + ENC_PARA. Debounce HW. |
| FS×4 | SPST momentary | THT | THT | Controlados por MCU + HEF4053BT (no 3PDT). |
| TVS_IN | P6KE18CA | DO-15 | THT | BV 17.1 V bidir, entrada instrumento. |
| TVS_12V | P6KE15CA | DO-15 | THT | BV 14.4 V, rail 12 V pre-regulador. (ECO 12 V: la P6KE12CA previa clampea 0.6 V *por debajo* del riel y no protege.) |
| TVS_USB | CDSOD323-T05LC | SOT-323 | SMD | Low-cap 2.5 pF, datos USB. |
| TVS_DC | SMAJ15A | SMA | SMD | Entrada DC (protección polaridad). |
| TVS_OUT | P6KE12CA (×3: OUT-L/OUT-R/HP) | DO-15 | THT | Regla 13 ESD salidas no balanceadas. |
| C_esd | 47 pF C0G/NP0 (×5: IN-L/IN-R/OUT-L/OUT-R/HP) | THT | THT | Regla 13 anti-EMI RF dupla TVS al CHASIS. |
| PTC | 500 mA rearmable | axial | THT | Protección entrada DC. |
| FB×6 | Ferrite bead BLM18 (IN-L/IN-R/OUT-L/OUT-R/HP/DC) | 0603 | SMD | Anti-RFI Regla 13 en cada jack externo. |
| C_in | 10 µF ‖ 100 nF (acople DC wideband) | THT | THT | Cubre graves + transitorios. |
| C_alias | 1 nF C0G/NP0 | THT | THT | Anti-aliasing PCM1808. |
| C_block_ADC | 10 µF (acople AC midrail +2.5 V) | THT | THT | Referencia 0 V TL072 ↔ +2.5 V VCOM. |
| C_pump | 10 µF (bombeo LT1054) | THT | THT | C1+/C1− del LT1054. |
| C_chgpump | 2.2 µF X7R (charge-pump PCM5102A) | SMD | SMD | CAPP/CAPM del PCM5102A. |
| R_div | 15 kΩ / 4.7 kΩ (divisor Thévenin) | THT | THT | Gain staging ADC. |
| L1 | Inductor 4.7 µH (filtro LC entrada) | THT | THT | Anti-ripple SMPS. |

---

## 7. Deudas técnicas y notas para Fase 3.1 (SKiDL)

| ID | Deuda | Severidad | Acción |
|:--:|---|:--:|---|
| 🟨 Símbolos custom | HEF4053BT, PCM1808, PCM5102A sin símbolo KiCad custom físico | 🟨 | Crear símbolos contrastando datasheet (rules/13 §13.5/§13.8/§13.9). Mientras tanto, `wiring_logic.py` los instancia como conectores numéricos (stubs ERC). |
| 🔶 NC relay PCB | Confirmar contacto NC de G5V-1 rutheado físicamente a GND | 🔶 | Verificar en esquemático de Fase 2/PCB. |
| 🔶 T1 | NeoPixel rojo de fault térmico sin integrar | 🔶 | Conectar `updateNeoPixels()` a `FaultLatch` (`main.cpp:245`). |
| 🟨 Ripple/PSRR riel | No medible con testbench actual del motor | 🟨 | `# TODO (gap testbench motor)` en `spice_targets.yaml` (Prompt 3 D.6). |

**Re-auditoría de firmware (regla 11):** las 3 deudas históricas del spec (**D2** sample rate SSOT, **D4** Safe Bypass físico, **D5** naming TEMP_ADC) están **RESUELTAS** en firmware (`HardwareConfig.h`, `main.cpp`). No se propaga deuda P1 al SKiDL. El Prompt 3 puede proceder sin pausa por deuda P1.

**Decisiones abiertas (regla 12):** las 3 del spec v1.0 (LT1054, SPST+HEF4053, estéreo) están **RESUELTAS** y retro-propagadas (spec §Próximo paso). No hay decisiones abiertas para esta Fase 2.

---

*Documento generado siguiendo Prompt 2 v1.4 (canon Hardware-As-Code v1.3). Ruteo listo para extracción SKiDL por Prompt 3 v2.6. Cero alucinación: cada afirmación cita `<archivo:línea>` o `rules/X §X.Y`.*
