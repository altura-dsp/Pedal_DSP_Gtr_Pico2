# Diagrama de Conexiones — Pedal DSP GTR

> **Versión:** 1.0 — Septiembre 2026 (documentación derivada · Prompt 6 v2.0)
> **Proyecto:** `1_Pedal_DSP_GTR`
> **Dominio:** 🎸 **Touring Grade**
> **Fecha:** Septiembre 2026
> **Base de verdad:** `Arquitectura_Hardware.md v1.2` (SSOT primaria, certificada SPICE 6/6 PASS)
> **Propósito:** vista de ruteo físico lista para el técnico que arma/mide el pedal. **Todo está expresado como `Componente · Pin → Componente · Pin`.**

---

## 0. Visión general del sistema

```
 ┌─────────┐   ┌──────────┐   ┌─────────┐   ┌──────────┐   ┌─────────┐   ┌──────────┐
 │ GUITARRA├──▶│ PREAMP   ├──▶│  ADC    ├──▶│   DSP    ├──▶│   DAC   ├──▶│  SALIDA  │
 │ IN-L/R  │   │ TL072    │   │PCM1808  │   │ RP2350   │   │PCM5102A │   │ NE5532 + │
 │ (jack)  │   │ JFET ×2  │   │24b/48k  │   │ dual-core│   │24b I2S  │   │ NJM4556A │
 └─────────┘   └──────────┘   └─────────┘   └──────────┘   └──────────┘   └──────────┘
       ▲              ▲             ▲              ▲              ▲             ▲
       │              │             │              │              │             │
   Protección     ±12 V rail    I2S + MCLK    12.288 MHz     I2S (PLL)    ±12 V rail
   TVS+ferrite    (LT1054)      (GPIO14/15/   anti-jitter    interno      (LT1054) +
   + C47pF                      17/21)                                    Safe Bypass
   al CHASIS                                                              (HEF4053)
```

**Flujo de señal resumido:**
`Guitarra → Jack IN-L/R → PTC+ferrite+TVS+RC → TL072 (preamp JFET) → divisor Thévenin → PCM1808 (ADC) → I2S → RP2350 (DSP Core 1) → I2S → PCM5102A (DAC) → NE5532 (buffer línea) / NJM4556A (HP) → Mute Relay G5V-1 → Jack OUT-L/R + HP`

---

## 1. Cadena de ENTRADA — Guitarra → ADC PCM1808 (estéreo L+R)

> Topología: protección jack → acople DC wideband → divisor Thévenin (anti-aliasing) → TL072 (preamp JFET) → red RC VREF/VCOM del ADC → PCM1808.

### 1.1 Protección y acople (canal L — el R es idéntico)

```
Jack IN-L · TIP ─┬─ PTC rearmable 500 mA ── (protección entrada)
                 ├─ Ferrite bead BLM18 ──── (anti-RFI)
                 ├─ TVS P6KE18CA · A ── Jack IN-L · TIP
                 ├─ TVS P6KE18CA · K ── CHASIS GND        (barrera TVS al CHASIS, no audio GND)
                 └─ C 47 pF C0G/NP0 ── Jack IN-L · TIP → CHASIS GND   (anti-EMI RF, dupla TVS+C47pF)

Jack IN-L · TIP ─┬─ C_in1A 10 µF electrolítico · + ─┐  (acople DC wideband)
                 └─ C_in1B 100 nF C0G/NP0          ┘  ‖ en paralelo → nodo IN_L_AC
```

### 1.2 Divisor Thévenin + preamp TL072

```
IN_L_AC ── R1 15 kΩ ── (R2 4.7 kΩ ── AGND) ── nodo DIV_L       (divisor Thévenin, gain staging)
                          │
                          └─ C_alias 1 nF C0G/NP0 ── AGND      (anti-aliasing — C0G/NP0 obligatorio)

DIV_L ── TL072 · 3 (IN_A+)                                     (TL072 DIP-8, rules/13 §13.1)
TL072 · 2 (IN_A−) ── R_fb 470 kΩ ── TL072 · 1 (OUT_A)          (realimentación, gain ×5 humbucker hot)
TL072 · 4 (V−) ── VEE_12V  (rail −12 V del LT1054)
TL072 · 8 (V+) ── VCC_12V  (rail +12 V)

TL072 · 1 (OUT_A) ── C_block_ADC 10 µF ── PCM1808 · 4 (VINL)   (acople AC al midrail +2.5 V del ADC)
```

### 1.3 Canal R (simétrico)

```
Jack IN-R · TIP ── [misma red de protección que IN-L] ── TL072 mitad B:
TL072 · 5 (IN_B+) ── [divisor R]
TL072 · 6 (IN_B−) ── R_fb 470 kΩ ── TL072 · 7 (OUT_B)
TL072 · 7 (OUT_B) ── C_block_ADC 10 µF ── PCM1808 · 5 (VINR)
```

> ✅ **Estéreo confirmado en firmware:** `AudioSystemI2S.cpp:111 read24(&inLeft,&inRight)` — el codec y el DSP procesan L+R.

---

## 2. Codecs (PCM1808 ADC + PCM5102A DAC)

### 2.1 ADC PCM1808 (TSSOP-16, I2S Slave, requiere MCLK externo)

| Pin | Conexión | Origen |
|:---:|---|---|
| `1` VREF | Red RC: 10 µF ‖ 100 nF ── AGND | rules/13 §13.8 Ley 9 |
| `2` VCOM | Red RC: 10 µF ‖ 100 nF ── AGND | rules/13 §13.8 Ley 9 |
| `3` AGND | AGND | — |
| `4` VINL | ← TL072 · 1 (OUT_A) vía C_block_ADC | §1.2 |
| `5` VINR | ← TL072 · 7 (OUT_B) vía C_block_ADC | §1.3 |
| `6` VCC | +5 V (rail analógico) | — |
| `7` FMT | Strap a DGND (I2S mode) | SBAS099 |
| `8` LRCK | ← RP2350 · GPIO15 | HardwareConfig.h:33 |
| `9` DOUT | → RP2350 · GPIO17 | HardwareConfig.h:37 |
| `10` BCK | ← RP2350 · GPIO14 | HardwareConfig.h:29 |
| `11` SCKI (MCLK) | ← RP2350 · GPIO21 (clk_gpout0, 12.288 MHz) | main.cpp:131-136 |
| `12` MD0 | Strap a DGND (slave) | SBAS099 |
| `13` MD1 | Strap a DGND (slave) | SBAS099 |
| `14` DGND | DGND | — |
| `15` VDD | +3.3 V (desde **AP2112K aislado**) | §5 |
| `16` PD | → +3.3 V (operación) | — |

### 2.2 DAC PCM5102A (TSSOP-20, PLL interno — NO requiere MCLK)

| Pin | Conexión | Origen |
|:---:|---|---|
| `1` CAPP | C_chgpump 2.2 µF X7R · pin 1 | rules/13 §13.9 |
| `2` CAPM | C_chgpump 2.2 µF X7R · pin 2 | rules/13 §13.9 |
| `3` VNEG | (riel interno — NO conectar) | — |
| `4` AGND | AGND | — |
| `5` AVDD | +3.3 V (desde **AP2112K aislado**) | §5 |
| `11` SCL | ── GND (PLL interno self-clocking) | rules/13 §13.9 Ley 10 |
| `13` BCK | ← RP2350 · GPIO14 | HardwareConfig.h:29 |
| `14` DIN | ← RP2350 · GPIO16 | HardwareConfig.h:41 |
| `15` LRCK | ← RP2350 · GPIO15 | HardwareConfig.h:33 |
| `17` XSMT | → **+3.3 V fijo** (NO flotante — sin esto NO hay sonido) | rules/13 §13.9 Ley 10 |

> 🔒 **3 invariantes Touring Grade del PCM5102A:** (1) XSMT a 3.3 V; (2) SCL a GND (no MCLK); (3) AVDD a 3.3 V desde AP2112K aislado.

---

## 3. Cadena de SALIDA — DAC → Salidas (ESTÉREO + Auriculares)

### 3.1 Salida principal estéreo (L+R) vía NE5532

```
PCM5102A · VOUTL ── C_out_L 10 µF ── NE5532 · 3 (IN_A+)
NE5532 · 2 (IN_A−) ── NE5532 · 1 (OUT_A)                       (buffer unidad, gain=1)
NE5532 · 4 (V−) ── VEE_12V ; NE5532 · 8 (V+) ── VCC_12V
NE5532 · 1 (OUT_A) ── R_iso 100 Ω ── G5V-1 · 5 (COM)
G5V-1 · 10 (NO) ── nodo OUT_L_JACK                             (mute: contacto NC a GND — ver §4)
OUT_L_JACK ── Ferrite BLM18 ── Jack OUT-L · TIP                (anti-RFI)
OUT_L_JACK ── TVS P6KE12CA · A ; TVS · K ── CHASIS GND          (Regla 13)
OUT_L_JACK ── C 47 pF C0G/NP0 → CHASIS GND                      (dupla Regla 13)
```

**Canal R** simétrico usando mitad B del NE5532 (`5/6/7`) → segundo relé G5V-1 → Jack OUT-R · TIP (con su propia TVS + C47pF + ferrite).

### 3.2 Auriculares (downmix L+R → NJM4556A high-current)

```
NE5532 · 1 (OUT_A) ── R_mix 10 kΩ ─┐
NE5532 · 7 (OUT_B) ── R_mix 10 kΩ ─┴─ nodo HP_MIX               (downmix pasivo L+R)
HP_MIX ── C_hp 10 µF ── NJM4556A · 3 (IN_A+)
NJM4556A · 2 (IN_A−) ── NJM4556A · 1 (OUT_A) ── R_iso_HP 22 Ω ── nodo HP_JACK
HP_JACK ── Ferrite BLM18 ── Jack HP · TIP
HP_JACK ── TVS P6KE12CA · A ; TVS · K ── CHASIS GND              (Regla 13)
HP_JACK ── C 47 pF C0G/NP0 → CHASIS GND                          (dupla Regla 13)
NJM4556A · 4 (V−) ── VEE_12V ; · 8 (V+) ── VCC_12V
```

> **Por qué NJM4556A y no NE5532 para HP:** NE5532 (±38 mA) no drivea 32 Ω con headroom; NJM4556A (±73 mA) sí.

> **Anti-Phantom OMITIDO (KISS/YAGNI):** la salida es TS/TRS **no balanceada** (pedal, no XLR) → no hay +48 V. Se omite la red anti-phantom. **PERO** el blindaje ESD/RFI del conector **NO se omite** (Regla 13, ortogonal).

---

## 4. Mute Relay + Safe Bypass (P1 Safety — el audio nunca se detiene)

### 4.1 Mute Relay G5V-1 (Omron, DIP — rules/13 §13.4)

```
RP2350 · GPIO23 (PIN_MUTE_RELAY) ── R_base 1 kΩ ── 2N3904 · 2 (B)     (drive por transistor)
2N3904 · 1 (E) ── GND ; 2N3904 · 3 (C) ── G5V-1 · 9 (Coil−)
G5V-1 · 2 (Coil+) ── +5 V                                            (rules/13 §13.4 Ley 5)
D1 1N4148 · K ── G5V-1 · 2 (Coil+) ; D1 · A ── G5V-1 · 9 (Coil−)     (diodo flyback innegociable)
G5V-1 · 5/6 (COM) ── path de salida (ver §3.1)
G5V-1 · 10 (NO) ── Jack OUT · TIP
```

> **Topología Touring Grade:** contacto **NC a tierra** — sin VCC o en boot, salida derivada a GND (silencio seguro). Requiere `HIGH` del firmware (`main.cpp:110-117`) para abrir el path. Secuencia boot: `MUTE=LOW → delay(500) → MUTE=HIGH`.

### 4.2 Safe Bypass HEF4053BT (Nexperia SOT109-1 — rules/13 §13.5)

```
RP2350 · GPIO24 (PIN_BYPASS_CTRL) ── R_pulldown 100 kΩ ── GND        (FAIL-SAFE: cae a LOW = bypass ON)
RP2350 · GPIO24 ── HEF4053BT · 10 (S1/nA, select switch 1)
HEF4053BT · 11 (Z1, común switch 1) ── nodo OUT_POST_DAC              (salida DSP)
HEF4053BT · 12 (Y11, canal HIGH) ── nodo IN_RAW                       (entrada directa = Dry Through)
HEF4053BT · 13 (Y10, canal LOW) ── red NC                             (canal sin uso)
HEF4053BT · 1 (S3), · 5 (Z2), · 4 (Z3) ── red NC                     (switches 2 y 3 sin uso)
HEF4053BT · 9 (S2/nB), · 15 (E/nE) ── GND                            (E=LOW habilita switches)
HEF4053BT · 16 (VDD) ── +3.3 V ; · 8 (VSS) ── GND ; · 14 (VEE) ── GND (single-supply)
```

> **Ley 6 HEF4053 Safe Bypass:** 1 solo switch del triple. Control S1 con **pull-down** → al perder VCC cae a LOW = Dry Through. Polaridad firmware (`main.cpp:119-122`): `HIGH = bypass ON`, `LOW = bypass OFF`. Fallo: DSP caído >3 s → bypass ON (`main.cpp:174-178`); 95 °C → bypass ON + halt (`main.cpp:233`).

---

## 5. Fuente de Alimentación (12 V DC → rieles)

> **Entrada LEÍDA DEL SSOT:** **12 V DC centro negativo** + USB-C desarrollo. Topología: protección → LT1054 (rail −) → Buck 5 V → LDOs 3.3 V (digital + analógico aislado AP2112K).

```
DC Jack 12 V · + ── PTC 500 mA ── TVS SMAJ15A ── MOSFET-P IRLML6401 ── nodo VCC_12V
DC Jack 12 V · − ── CHASIS GND                                       (protección inversión polaridad)

VCC_12V ── LT1054 · 8 (V+)                                           (rules/13 §13.3 DIP-8)
LT1054 · 7 (OSC) ── LT1054 · 8 (V+) vía R_boost 20 kΩ               (modo BOOST >35 kHz inaudible)
LT1054 · 1 (FB) ── LT1054 · 5 (Vout)                                 (realimentación)
LT1054 · 2 (C1+) ── C_pump 10 µF · + ── LT1054 · 4 (C1−)            (cap de bombeo)
LT1054 · 3 (GND) ── AGND
LT1054 · 5 (Vout) ── nodo VEE_12V  (rail negativo −12 V)            ← SPICE midió −12.000 V ✅

VCC_12V ── Buck Step-Down (5 V) ── nodo VCC_5V                       (Pico VSYS + NeoPixels)
VCC_5V  ── LDO 3.3 V (digital) ── nodo VCC_3V3_DIG                  (RP2350, OLED, NeoPixels)
VCC_5V  ── AP2112K-3.3 (analógico aislado) ── nodo VCC_3V3_ANA       (codecs PCM1808/PCM5102A)
   AP2112K · 1 (VIN) ── VCC_5V
   AP2112K · 2 (GND) ── AGND
   AP2112K · 3 (EN) ── VCC_5V  (always-on)
   AP2112K · 4 (BP/NR) ── C_bp 10 nF ── AGND
   AP2112K · 5 (VOUT) ── VCC_3V3_ANA ; C_out 4.7 µF tantalio ── AGND
```

> **LT1054 sobre ICL7660S:** ~100 mA vs ~40 mA → margen para TL072+NE5532+NJM4556A cargados. A **12 V de entrada** es la única opción segura: ICL7660S AbsMax 12 V (sin margen, explota); LT1054 AbsMax 15 V (margen correcto).

### 5.1 Protección TVS pre-regulador (post-ECO 12V)

```
VCC_12V ── TVS_12V: P6KE15CA · A ── AGND                             (BV 14.4 V > 12 V rail ✅)
```

> ⚠️ **ECO 12V (Prompt 5):** la TVS previa era **P6KE12CA** (BV 11.4 V), que clampea **0.6 V por debajo** del riel de 12 V → no protege. Cambio a **P6KE15CA** (BV 14.4 V). Esta corrección vino del DRC Paridad SSOT post-SPICE.

### 5.2 Esquema de tierras (regla 7.10/7.33)

**Plano continuo + partición geométrica** + unión **ÚNICA** AGND/DGND/CHASIS en el conector DC. **PROHIBIDA la "Tierra en Estrella"** (antena de bucle con MCLK de 12.288 MHz). Las ferrite beads separan AGND↔DGND↔CHASIS con un único punto de unión monopunto.

---

## 6. Microcontrolador RP2350 + mapa GPIO

### 6.1 Relojes matemáticos

| Parámetro | Valor | Fuente |
|:----------|:------|:-------|
| Frecuencia MCU | **196 608 000 Hz** (196.608 MHz) | platformio.ini:5 |
| Sample Rate SSOT | **48 000 Hz** | HardwareConfig.h:24 |
| MCLK | **12.288 MHz** (256×48k) en GPIO21 | main.cpp:131-136 |
| VREG | `VREG_VOLTAGE_1_10` (sostener 196 MHz OC) | main.cpp:106 |
| Watchdog | 8000 ms | main.cpp:126 |

### 6.2 Mapa SSOT de GPIO (cero inventados)

| GPIO | Función | Pin físico Pico 2 |
|:---:|---|:---:|
| GPIO0 | OLED SDA (I2C) | 1 |
| GPIO1 | OLED SCL (I2C) | 2 |
| GPIO2 | ENC1 A | 4 |
| GPIO3 | ENC1 B | 5 |
| GPIO4 | ENC1 SW | 6 |
| GPIO5 | ENC2 A | 7 |
| GPIO6 | ENC2 B | 9 |
| GPIO7 | ENC2 SW | 10 |
| GPIO8 | ENC3 A | 11 |
| GPIO9 | ENC3 B | 12 |
| GPIO10 | ENC3 SW | 14 |
| GPIO11 | FS1 (footswitch) | 15 |
| GPIO12 | FS2 | 16 |
| GPIO13 | FS3 | 17 |
| **GPIO14** | **I2S BCLK** | 19 |
| **GPIO15** | **I2S LRCK** (= BCLK+1) | 20 |
| **GPIO16** | **I2S DOUT** (→ DAC) | 21 |
| **GPIO17** | **I2S DIN** (← ADC) | 22 |
| GPIO18 | ENC_PARA A | 24 |
| GPIO19 | ENC_PARA B | 25 |
| GPIO20 | ENC_PARA SW | 26 |
| **GPIO21** | **MCLK** (clk_gpout0) | 27 |
| GPIO22 | FS4 | 29 |
| **GPIO23** | **Mute Relay** | 31 |
| **GPIO24** | **Bypass CTRL** (HEF4053) | 32 |
| GPIO26 | Master Volume (ADC0) | 31 |
| GPIO27 | Expression Pedal (ADC1) | 32 |
| GPIO28 | NeoPixel (WS2812B) | 34 |
| GPIO29 | Entropy ADC (TRNG, flotante) | — |

> Mapeo GPIO→pin físico THT según `rules/13 §13.11` (`PICO2_GPIO_TO_PIN`). **GPIO25 libre** (LED on-board sin asignar). Auditoría anti-colisión: sin duplicaciones (28 GPIOs usados, todos distintos).

---

## 7. Interfaz de Usuario (Hardware Debouncing)

> Topología anti-rebote: `Señal → Red RC (10 kΩ pull-up + 100 nF a GND) → 74HC14 Schmitt → GPIO` (cero debouncing por software).

### 7.1 Encoders + Footswitches vía 74HC14 (hex Schmitt)

```
(Para cada encoder A/B y cada footswitch FSx):
Jack/Encoder · señal ── R_pull 10 kΩ ── +3.3 V_DIG                (pull-up)
                    ── C_deb 100 nF ── GND                         (filtro RC, τ = 1 ms)
                    ── 74HC14 · Ax (entrada inversor)              (rules/13 §13.7 DIP-14)
74HC14 · Yx (salida Schmitt limpio) ── RP2350 · GPIOx
74HC14 · 14 (VCC) ── VCC_3V3_DIG ; · 7 (GND) ── GND                (desde rail DIGITAL, no AP2112K)
```

**Asignación de inversores del 74HC14:**
- ENC1.A → 1A → 1Y → GPIO2 · ENC1.B → 2A → 2Y → GPIO3
- ENC2.A → 3A → 3Y → GPIO5 · ENC2.B → 4A → 4Y → GPIO6
- ENC3.A → 5A → 5Y → GPIO8 · ENC3.B → 6A → 6Y → GPIO9
- (FS1/FS2/FS3/FS4 + ENC*.SW → 2º 74HC14 o debounce por RC directo al GPIO)

### 7.2 NeoPixels (WS2812B, rules/13 §13.14)

```
RP2350 · GPIO28 ── R_data 470 Ω ── WS2812B[0] · 4 (DIN)            (level-shift opcional vía 74AHCT125)
WS2812B[i] · 2 (DOUT) ── WS2812B[i+1] · 4 (DIN)                    (cascada unidireccional)
WS2812B · 1 (VDD) ── VCC_5V + C 100 µF ‖ 100 nF                    (anti-inrush primer LED)
WS2812B · 3 (VSS) ── GND
```

### 7.3 OLED SSD1306 128×64 (I2C)

```
RP2350 · GPIO0 (SDA) ── OLED · SDA
RP2350 · GPIO1 (SCL) ── OLED · SCL
OLED · VCC ── VCC_3V3_DIG ; · GND ── GND
```

---

## 8. Lista de conectores externos (vista técnico-rápida)

| Conector | Pines físicos | Esquema de protección |
|:---------|:--------------|:----------------------|
| **DC Jack 12 V** | +, − (centro negativo) | PTC 500 mA · TVS SMAJ15A · MOSFET-P IRLML6401 (anti-inversión) |
| **IN-L / IN-R** | TIP, SLEEVE | PTC + Ferrite BLM18 + TVS P6KE18CA + C 47 pF (CHASIS) |
| **OUT-L / OUT-R** | TIP, SLEEVE | Ferrite + TVS P6KE12CA + C 47 pF (CHASIS), vía Mute Relay |
| **HP** | TIP, SLEEVE | Ferrite + TVS P6KE12CA + C 47 pF (CHASIS), vía NJM4556A |
| **USB-C** | D+, D−, VBUS | TVS CDSOD323-T05LC low-cap (2.5 pF) |
| **Expression** | TIP, RING, SLEEVE | Divisor resistivo al ADC (GPIO27) |

---

## 9. BOM rápido (referencia — detalle completo en `Arquitectura_Hardware.md §6`)

| Ref | Componente | Package |
|:----|:-----------|:--------|
| MCU | Raspberry Pi Pico 2 (RP2350) | módulo |
| ADC | PCM1808 | TSSOP-16 |
| DAC | PCM5102A | TSSOP-20 |
| U1 | TL072 (preamp L+R) | DIP-8 |
| U2 | NE5532 (salida línea L+R) | DIP-8 |
| U3 | NJM4556A (auriculares) | DIP-8 |
| U4 | LT1054 (inversor −12 V) | DIP-8 |
| U5 | AP2112K-3.3 (LDO audio aislado) | SOT-23-5 |
| U6/U7 | 74HC14 (debouncing Schmitt) | DIP-14 |
| U8 | HEF4053BT (Safe Bypass) | SO-16 |
| K1/K2 | G5V-1 (relé mute L y R) | DIP |
| Q1 | 2N3904 (drive relé) | TO-92 |
| Q2 | IRLML6401 (MOSFET-P polaridad) | SOT-23 |
| D1/D2 | 1N4148 (flyback relés) | DO-35 |
| LED_RGB | NeoPixel WS2812B ×8 | 5050 |
| OLED | SSD1306 128×64 (I2C) | módulo |
| ENC ×4 | EC11 con detente + switch | THT |
| FS ×4 | SPST momentary | THT |
| TVS_IN | P6KE18CA (BV 17.1 V) | DO-15 |
| **TVS_12V** | **P6KE15CA (BV 14.4 V)** — ECO 12V | DO-15 |
| TVS_OUT | P6KE12CA ×3 (OUT-L/R/HP) | DO-15 |

---

*Diagrama de Conexiones V1.0 — documentación derivada del `Arquitectura_Hardware.md v1.2` (SSOT primaria). Cero alucinación: cada ruteo cita `Componente · Pin → Componente · Pin` con fuente `rules/X §X.Y` o `<archivo>:<línea>`.*
