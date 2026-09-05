# 📋 SPICE Report — 1_Pedal_DSP_GTR

- **Fecha:** 2026-08-25 13:09:50
- **Dominio:** Touring Grade (hardware analógico)
- **Veredicto global:** ✅ **PASS** (6/6 targets OK)

## Resumen ejecutivo

| # | Banco | Tipo | Mediciones (medido vs límite) | Estado |
|:--|:------|:-----|:-------------------------------|:------:|
| 1 | `input_antialiasing` | ac | cutoff: medido 44469.0 Hz vs esperado 44000.0 Hz (err 1.07%, tol 10.0%) | ✅ **PASS** |
| 2 | `input_lowcut_dc` | ac | atenuacion @ 0.1 Hz: -30.66 dB (limite <= -20.0 dB) | ✅ **PASS** |
| 3 | `output_thd_line` | transient | THD: medido 0.0000% vs limite 0.1% | ✅ **PASS** |
| 4 | `output_headroom_line` | transient | Vout pico: 1.001 V vs max 3.0 V · Vout pico: 1.001 V vs min -3.0 V | ✅ **PASS** |
| 5 | `output_thd_hp_32ohm` | transient | THD: medido 0.0002% vs limite 0.1% | ✅ **PASS** |
| 6 | `power_inverter_rail` | transient | Vdc nodo generado: -11.991 V vs ventana [-13.0, -8.0] V | ✅ **PASS** |

---

### ✅ input_antialiasing (ac) -> `PASS`
- cutoff: medido 44469.0 Hz vs esperado 44000.0 Hz (err 1.07%, tol 10.0%) -> OK

---

### ✅ input_lowcut_dc (ac) -> `PASS`
- atenuacion @ 0.1 Hz: -30.66 dB (limite <= -20.0 dB) -> OK

---

### ✅ output_thd_line (transient) -> `PASS`
- THD: medido 0.0000% vs limite 0.1% -> OK

---

### ✅ output_headroom_line (transient) -> `PASS`
- Vout pico: 1.001 V vs max 3.0 V -> OK
- Vout pico: 1.001 V vs min -3.0 V -> OK

---

### ✅ output_thd_hp_32ohm (transient) -> `PASS`
- THD: medido 0.0002% vs limite 0.1% -> OK

---

### ✅ power_inverter_rail (transient) -> `PASS`
- Vdc nodo generado: -11.991 V vs ventana [-13.0, -8.0] V -> OK
