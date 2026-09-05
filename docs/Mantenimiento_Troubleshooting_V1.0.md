# Mantenimiento y Troubleshooting — Pedal DSP GTR

> **Versión:** 1.0 — Septiembre 2026 (documentación derivada · Prompt 6 v2.0)
> **Proyecto:** `1_Pedal_DSP_GTR`
> **Dominio:** 🎸 **Touring Grade** — mantenimiento de gira y diagnóstico de campo
> **Fecha:** Septiembre 2026
> **Base de verdad:** `Arquitectura_Hardware.md v1.2` (SSOT primaria) + `src/system/FaultLatch.h` + `src/main.cpp` + `src/utils/DSPMetrics.h`
> **Audiencia:** técnico de mantenimiento / roadie avanzado / desarrollador depurando un fault en vivo.

---

## 1. Filosofía de mantenimiento (Fire & Forget)

El pedal está diseñado para que **el audio nunca se detenga en un show** (CLAUDE.md Regla 5). Tres líneas defensivas:

```
   ┌─────────────────────────────────────────────────────────┐
   │ Línea 1 — PREVENIR  (no llegar al fault)                │
   │   · Térmico 85 °C → mute preventivo 100 ms              │
   │   · CPU Hard-Mute → anti-underrun                       │
   │   · Watchdog 8 s → cuelgues no cuelgan                  │
   ├─────────────────────────────────────────────────────────┤
   │ Línea 2 — SOBREVIVIR (el fault ya ocurrió, audio sigue) │
   │   · Safe Bypass HEF4053 → paso directo analógico        │
   │   · Mute Relay NC → silencio seguro en boot/reset       │
   │   · FaultLatch persistente → diagnóstico post-reset     │
   ├─────────────────────────────────────────────────────────┤
   │ Línea 3 — DIAGNOSTICAR (post-show, en bancada)          │
   │   · FaultLatch Scratch 5 + Serial DSPMetrics            │
   │   · MSPLIM stack guard + underrun counter                   │
   └─────────────────────────────────────────────────────────┘
```

---

## 2. FaultLatch — el grabador de faults persistente (`src/system/FaultLatch.h`)

### 2.1 Mecanismo de almacenamiento

Los faults se graban en el **Watchdog Scratch Register 5** del RP2350 (`FaultLatch.h:30`):

```cpp
watchdog_hw->scratch[5] |= (1 << fault);
```

> ⚠️ **Layout del registro (32 bits):**
> - **Bits 0–5:** flags individuales de cada tipo de fault.
> - **Bits 8–15:** contador total de faults (saturado en 255).
> - Bits 16–31: sin uso.

**Por qué Scratch 5 y no EEPROM/flash:** el scratch register del watchdog **sobrevive a cualquier reset** (incluido stack overflow y peralte térmico), y su escritura es **instantánea** (un OR atómico). El **Scratch 4 lo usa el bootloader de Earle Philhower** — se evita deliberadamente para no colisionar (`FaultLatch.h:14`).

### 2.2 Catálogo de faults (`FaultLatch.h:17-24`)

| `FaultType` | Bit | Significado | Disparador en código |
|:------------|:---:|:------------|:---------------------|
| `I2S_UNDERRUN` | 0 | El DMA I2S no alimentó el DAC a tiempo | `AudioSystemI2S` (underrun detectado) |
| `CPU_THROTTLE` | 1 | El CPU budget superó el umbral (Regla 10 <90%) | `AudioSystemI2S` CPU Hard-Mute |
| `THERMAL_THROTTLE` | 2 | Temperatura alcanzó **85 °C** | `main.cpp:244` |
| `I2C_ERROR` | 3 | Falla de comunicación OLED/NeoPixel | UIManager / DisplayManager |
| `STACK_OVERFLOW` | 4 | Stack overflow detectado (MSPLIM) | `isr_hardfault()` |
| `THERMAL_CRITICAL` | 5 | Temperatura alcanzó **95 °C** (riesgo físico) | `main.cpp:232` |

### 2.3 Lectura de diagnóstico (bancada)

Conectá USB-C y abrí el monitor Serial (115200 baud). El reporte `DSPMetrics::printReport()` (cada 5 s, `main.cpp:252`) incluye estado de métricas. Para inspeccionar el FaultLatch directo:

```cpp
Serial.print("Faults: ");
Serial.println(globalFaults.faultCount(), HEX);
if (globalFaults.stackOverflow())   Serial.println("  ⚠ STACK_OVERFLOW");
if (globalFaults.thermalCritical()) Serial.println("  ⚠ THERMAL_CRITICAL");
if (globalFaults.thermalThrottle()) Serial.println("  ⚠ THERMAL_THROTTLE");
if (globalFaults.cpuThrottle())     Serial.println("  ⚠ CPU_THROTTLE");
if (globalFaults.i2sUnderrun())     Serial.println("  ⚠ I2S_UNDERRUN");
if (globalFaults.i2cError())        Serial.println("  ⚠ I2C_ERROR");
```

**Reset tras diagnóstico confirmado:** `globalFaults.reset();` (`FaultLatch.h:40`) limpia `scratch[5]`.

---

## 3. Jerarquía térmica (`main.cpp:226-248`)

Lectura cada 5 s vía `DSPMetrics::updateTemperature()` (ADC canal interno `ADC_CHANNEL_TEMP=4`, **NO** GPIO4 físico — `HardwareConfig.h:55`).

| Umbral | Acción automática | Fault registrado | Línea |
|:-------|:------------------|:-----------------|:------|
| **<85 °C** | Operación normal | — | — |
| **≥85 °C** (advertencia) | **Mute preventivo 100 ms** (LOW→delay(100)→HIGH) + LED rojo parpadeante (TODO) | `THERMAL_THROTTLE` | `main.cpp:240-246` |
| **≥95 °C** (crítico, riesgo físico) | **Safe Bypass ON + emergencyMute + halt** (reboot controlado por watchdog 8 s tras enfriar) | `THERMAL_CRITICAL` | `main.cpp:231-237` |

> **P1 Safety > P2 No-Blocking:** el `delay(100)` del mute preventivo es la **única excepción** a la regla No-Blocking UI — corre en Core 1 (no hot-path) y es prioridad de seguridad física.

### 3.1 Diagnóstico térmico en gira

| Síntoma visual | Causa probable | Acción |
|:---------------|:---------------|:-------|
| NeoPixel rojo parpadeante | 85 °C alcanzado (mute preventivo activado) | Mejorar ventilación, bajar carga DSP (quitar un efecto) |
| Audio "seco" sin efectos tras calentamiento | Safe Bypass térmico (95 °C) | Apagar, dejar enfriar 5 min, reiniciar |
| Reinicios cíclicos | Halt térmico persistente (>95 °C sostenido) | Revisar disipación del RP2350, ventilación del enclosure |

---

## 4. Safe Bypass — diagnóstico (`main.cpp:166-178`)

### 4.1 State machine del bypass

```
dsp_alive.exchange(false) cada loop() Core 0:
  ├── true (Core 1 marcó vivo) → watchdog_update() · bypass=LOW · systemArmed=true
  └── false:
       ├── !systemArmed → (aún no arrancó, esperar)
       └── systemArmed && (millis() − lastHeartbeat > 3000 ms)
                                  → bypass=HIGH  ← DSP caído, audio por HEF4053
```

### 4.2 Síntomas y diagnóstico

| Síntoma | Causa | Verificación |
|:--------|:------|:-------------|
| **Audio "seco" (paso directo) sin efecto, persistente** | DSP caído >3 s → bypass activado | `faultCount > 0` en Serial; revisar `STACK_OVERFLOW`/`THERMAL_*` |
| Audio normal pero NeoPixel/no-response UI | Core 1 caído, Core 0 sigue | El bypass **no** se activa (Core 0 sigue viteando); revisar `I2C_ERROR` |
| Bypass parpadea entre on/off | dsp_alive intermitente (Core 1 inestable) | Stress test: cargar 4 efectos pesados + tunar; mirar CPU budget |

### 4.3 Polaridad del bypass — referencia rápida

| `PIN_BYPASS_CTRL` (GPIO24) | Estado |
|:---------------------------|:-------|
| `LOW` | Audio por **DSP** (modo normal) |
| `HIGH` | **Safe Bypass ON** (audio entrada→salida directo, HEF4053) |

> 🔒 **FAIL-SAFE hardware:** el HEF4053 tiene **pull-down 100 kΩ** en S1 (Regla 13). Al perder VCC, el pin cae a LOW = bypass OFF… **pero** el control firmware lo overridea a HIGH ante fault. El path NC del HEF4053 y el Mute Relay NC garantizan silencio/seguridad en ausencia de señal.

---

## 5. Mute Relay G5V-1 — boot y troubleshooting (`main.cpp:110-117`)

### 5.1 Secuencia de boot (fire & forget)

```
T=0     MUTE_RELAY = LOW          ← NC a tierra: SILENCIO absoluto
T=0     delay(500)                ← estabilización fuente (anti-pop amplificador)
T=500   MUTE_RELAY = HIGH         ← libera el audio
```

> El contacto **NC (Normally Closed) deriva a tierra**. Sin VCC o durante boot, la salida está cortocircuitada a GND → **cero pop en el amplificador**. Esto es **Touring Grade**, no opcional.

### 5.2 Troubleshooting del relé

| Síntoma | Causa | Acción |
|:--------|:------|:-------|
| Pop fuerte al encender | Relé no llega a cerrar NC antes del delay, o flyback diodo abierto | Verificar `D1 1N4148` flyback (`Diagrama_Conexiones §4.1`); medir continuidad NC |
| Sin audio permanente | Relé pegado en NC (coil no energiza) | Medir +5 V en `G5V-1 · 2`; verificar `Q1 2N3904` y `R_base 1 kΩ` |
| Click audible cada ~5 s | Mute térmico preventivo (85 °C) | Ver §3 — mejorar ventilación |
| Reset con pop audible | `emergencyMute()` no alcanzó a bajar pines | Revisar `main.cpp:54-64` (BCLK/DOUT a LOW antes de reset) |

---

## 6. Watchdog y heartbeat (`main.cpp:124-178`)

### 6.1 Watchdog

```cpp
watchdog_enable(8000, true);   // main.cpp:126 — 8000 ms, pause-on-debug
```
- **8 s** de margen (expandido desde 1.6 s para soportar estrés a 196 MHz).
- Si Core 0 no llama `watchdog_update()` en 8 s → reboot automático.
- El halt térmico (95 °C) **abusa** del watchdog a propósito: `while(true)` sin `watchdog_update` → reboot tras enfriar (`main.cpp:236`).

### 6.2 Heartbeat atómico

`std::atomic<bool> dsp_alive` (`main.cpp:33`):
- **Core 1** (`main.cpp:269`): `dsp_alive.store(true)` cada `loop1()`.
- **Core 0** (`main.cpp:168`): `dsp_alive.exchange(false)` — lee y resetea atómico.

Si el Core 1 (UI/moduladores/telemetría) crashea, el Core 0 deja de ver el pulso → tras 3 s activa Safe Bypass (§4). El audio sigue.

---

## 7. Hardware Stack Guard (MSPLIM)

El RP2350 utiliza el registro MSPLIM (Main Stack Pointer Limit) integrado en el Cortex-M33. Si la pila desborda, el hardware dispara un `HardFault` interceptado por `isr_hardfault()` (`main.cpp`), que fuerza `emergencyMute()` y reinicia. Ya no se usan canarios de software.

## 8. Tabla maestra de troubleshooting (síntoma → causa → acción)

| # | Síntoma | Causa probable | Acción inmediata | Diagnóstico post-show |
|:--|:--------|:---------------|:-----------------|:---------------------|
| 1 | Sin audio al boot | Mute no libera / I2S no init | Esperar 2 s; verificar fuente **12 V DC** | `audioSystem.init()` retornó false (`main.cpp:142`); medir MCLK 12.288 MHz en GPIO21 |
| 2 | Pop/click al conectar fuente | Fuente conectada antes que el resto | Conectar **12 V en último lugar** | Secuencia correcta: cables → fuente |
| 3 | Audio "seco" sin efectos | Safe Bypass (DSP caído >3 s) | Reiniciar deliberadamente | `STACK_OVERFLOW`/`THERMAL_*` en Scratch 5 |
| 4 | Glitches/clicks bajo carga DSP | CPU underrun | Quitar un efecto (4→3) | `I2S_UNDERRUN` + `getUnderrunCount()` alto |
| 5 | Reinicios cada ~8 s | Watchdog timeout (cuelgue Core 0) | Reducir carga DSP | `CPU_THROTTLE` activo; CPU budget >90% |
| 6 | Reinicios cada ~5 min | Halt térmico (95 °C sostenido) | Mejorar ventilación | `THERMAL_CRITICAL` en Scratch 5 |
| 7 | Mute intermitente (~cada 5 s) | Térmico 85 °C preventivo | Ventilar el pedal | `THERMAL_THROTTLE`; revisar disipación RP2350 |
| 8 | OLED no responde | I2C colisionado / OLED suelto | Reiniciar; verificar cable I2C | `I2C_ERROR` en Scratch 5 |
| 9 | NeoPixels no actualizan | WS2812 data suelto / GPIO28 | Verificar `R_data 470 Ω` y VCC 5 V | — |
| 10 | Distorsión no musical a alto vol | Gain de entrada satura TL072 | Bajar vol. guitarra o gain primer efecto | Validar con osciloscopio en `TL072 · 1 (OUT_A)` |
| 11 | Zumbido 50/60 Hz | Loop de masa (daisy-chain) | Fuente aislada o iso-transformer | Medir continuidad GND entre pedals |
| 12 | Latencia notable | (no esperado) buffers 2×32 = 1.33 ms | Verificar que no hay efecto con latency oculta | `getUnderrunCount()`; revisar delays con crossfade cargado |
| 13 | Cambio de escena con click | Crossfade muy corto / efecto sin spill-over | Verificar `CROSSFADE_COEF=0.005` | Revisar `setTailOnly` en reverb/delay |
| 14 | Encoder no responde | Debouncing 74HC14 / RC sucio | CALIBRATION; limpiar contacto | Osciloscopio en salida 74HC14 |
| 15 | Fuente se calienta | Corto en PCB / inversor LT1054 sobrecargado | **DESCONECTAR** inmediatamente | Medir consumo (<500 mA típico); revisar `VEE_12V` corto a GND |

---

## 9. Mantenimiento preventivo (checklist de gira)

### 9.1 Pre-show (todos los días)

- [ ] **Fuente:** 12 V DC centro negativo, verified con multímetro (12.0–12.6 V).
- [ ] **Cables IN/OUT:** probados con señal. Jacks sin holgura.
- [ ] **Enclosure:** ventilación libre (no apilar sobre amps valvulares).
- [ ] **NeoPixels:** limpios (polvo difumina el color → ilegible a 2 m).
- [ ] **Boot test:** encender, verificar secuencia mute (silencio → audio en <2 s).
- [ ] **Safe Bypass test:** desconectar y reconectar USB-C durante operación → audio sigue.

### 9.2 Semanal (en gira larga)

- [ ] Limpiar jacks 6.35 mm con spray contact cleaner.
- [ ] Verificar tornillos de enclosure (vibración de viaje).
- [ ] Backup de presets (USB-C → PC).
- [ ] Inspección visual de PCB (capacitores hinchados, soldaduras frías).

### 9.3 Mensual / entre giras

- [ ] Stress test: 4 efectos pesados + tuner + expression durante 1 h. Verificar `underrunCount` y `faultCount == 0`.
- [ ] Medir consumo total (<500 mA típico).
- [ ] Medir rieles: VCC_12V (+12 V), VEE_12V (−12 V), VCC_5V, VCC_3V3_DIG, VCC_3V3_ANA.
- [ ] Validar MCLK 12.288 MHz con osciloscopio/frecuencímetro en GPIO21.

---

## 10. Mantenimiento correctivo (procedimientos)

### 10.1 Reset de FaultLatch (tras diagnóstico confirmado)

```cpp
globalFaults.reset();   // limpia watchdog_hw->scratch[5]
```
Solo tras haber **leído y registrado** el fault. Si reseteás sin diagnosticar, perdés la trazabilidad del problema.

### 10.2 Reproceso de un THERMAL_CRITICAL recurrente

1. Confirmar con `globalFaults.thermalCritical()`.
2. Stress test en bancada con ventilación controlada.
3. Si repite con carga DSP baja → **problema de disipación**: verificar pad térmico del RP2350 al PCB, aplicar heatsink.
4. Si repite con carga DSP alta → **reducir `MAX_EFFECTS_SIMULTANEOUS`** o efecto ofensor (auditar CPU budget en `DSPMetrics`).

### 10.3 Reproceso de STACK_OVERFLOW

1. Confirmar `globalFaults.stackOverflow()`.
2. En bancada: instrumentar `processBlock` con un high-water mark de pila (p.ej. trazar `__builtin_frame_address` entre llamadas) **fuera** del handler — **nunca** dentro de `isr_hardfault()` (pila corrupta → HardFault en cascada, BUG FIX #4). El debug va en código de diagnóstico, no en el handler.
3. Auditar `processBlock` de efectos sospechosos: buscar arrays locales, recursion, funciones no inlineadas.
4. **Regla 6 CLAUDE.md:** buffers >256 elementos → static o init-time heap.

---

## 11. Repuestos críticos (BOM mínimo de gira)

> Lista corta para llevar en el flight case. BOM completo en `Arquitectura_Hardware.md §6`.

| Ref | Componente | Por qué llevarlo |
|:----|:-----------|:-----------------|
| G5V-1 | Relé mute (×2, L+R) | Fail = sin audio o pop permanente |
| 1N4148 | Diodo flyback (×2) | Fail = relé daña Q1 al apagar |
| 2N3904 | Transistor drive relé | Fail = relé no opera |
| P6KE15CA | TVS rail 12 V | Fail = sobretensión mata LT1054/TL072 |
| P6KE18CA | TVS entrada (×2) | Fail = ESD mata preamp |
| LT1054 | Inversor −12 V | Fail = sin rail negativo → sin audio line-out |
| AP2112K-3.3 | LDO audio aislado | Fail = codecs sin alimentación |
| PCM1808 / PCM5102A | ADC / DAC | Fail = cadena rota (no field-reparable fácil) |
| TL072 / NE5532 / NJM4556A | Opamps | Field-reparable DIP-8 |
| HEF4053BT | Safe Bypass | Fail = bypass no conmuta |
| Pico 2 (RP2350) | MCU | Llevar 1 programado idéntico como cold spare |

---

## 12. Escalado de un problema

| Si el problema es… | Escalar a… |
|:-------------------|:-----------|
| Mute/relé/bypass (P1 Safety) | **Inmediato** — no salir de gira sin resolver |
| Stack overflow / watchdog | **Antes del próximo show** — riesgo de Safe Bypass en vivo |
| Glitch DSP / underrun | Optimizar (quitar efecto) — tolerable si intermitente |
| OLED ilegible | Bajo — usar NeoPixels como guía |
| Cosmetic (rayón, LED suelto) | Post-gira |

---

*Mantenimiento y Troubleshooting V1.0 — documentación derivada del `Arquitectura_Hardware.md v1.2` (SSOT primaria) con cross-check de `src/system/FaultLatch.h`, `src/main.cpp`, `src/utils/DSPMetrics.h`. Cero alucinación: cada fault, umbral y línea de código cita su fuente. Para el punto de vista del músico, ver `Manual_Usuario_V1.0.md` §10; para firmware, `Guia_Desarrollador_V1.0.md`.*
