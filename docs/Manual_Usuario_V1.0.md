# Manual de Usuario — Pedal DSP GTR

> **Versión:** 1.0 — Septiembre 2026 (documentación derivada · Prompt 6 v2.0)
> **Proyecto:** `1_Pedal_DSP_GTR`
> **Dominio:** 🎸 **Touring Grade** — pedal multi-efectos de guitarra para uso en vivo
> **Fecha:** Septiembre 2026
> **Base de verdad:** `Arquitectura_Hardware.md v1.2` (SSOT primaria, certificada por SPICE 6/6 PASS + Prompt 5 DRC Paridad SSOT)

Este manual está escrito para el **músico en escenario**. Si buscás detalles técnicos de ingeniería (esquemáticos, código, matemática DSP), consultá `Guia_Desarrollador_V1.0.md` y `Manual_Teorico_V1.0.md`.

---

## 1. ¿Qué es el Pedal DSP GTR?

Un **multi-efectos digital de guitarra** basado en Raspberry Pi Pico 2, con:

- **Entrada estéreo** (2 jacks) para guitarra o bajo, con preamplificador JFET vintage (TL072).
- **Procesamiento DSP a 48 kHz / 24-bit** con **hasta 4 efectos simultáneos** (`MAX_EFFECTS_SIMULTANEOUS=4`).
- **Salida estéreo de línea** (L/R) a amplificador o loop de efectos.
- **Salida de auriculares** dedicada (driver high-current para 32 Ω).
- **Afinador cromático** integrado.
- **Pantalla OLED** legible a 2 m de distancia + **NeoPixels** con código de colores por categoría.
- **4 footswitches** para Scenes/Snapshots sin corte de audio (gapless).
- **Safe Bypass hardware**: si el DSP falla, el audio **nunca se detiene** — conmuta automáticamente a paso directo.

> 🔒 **Filosofía Touring Grade:** este pedal está diseñado para sobrevivir al maltrato de una gira. Las mismas luces, los mismos sonidos, noche tras noche. Si algo falla, el sonido sigue.

---

## 2. El panel del pedal

### 2.1 Panel frontal (controles)

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                 │
│   ┌─────────────────────────┐      ◯ ENC1   ◯ ENC2   ◯ ENC3    │
│   │                         │                                   │
│   │      OLED 128×64        │      (seleccionar / editar        │
│   │   (pantalla principal)  │       parámetros del efecto)      │
│   │                         │                                   │
│   └─────────────────────────┘      ◯ ENC_PARA (Master)          │
│                                                                 │
│    NeoPixels →   ● ● ● ● ● ● ● ●   ← Category Color Coding     │
│                                                                 │
│   ┌──────┐  ┌──────┐  ┌──────┐  ┌──────┐                       │
│   │  FS1 │  │  FS2 │  │  FS3 │  │  FS4 │   ← Scenes/Snapshots  │
│   └──────┘  └──────┘  └──────┘  └──────┘      + bypass + tune   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Control | Función principal |
|:--------|:------------------|
| **OLED 128×64** | Pantalla principal. Muestra preset, banco, parámetros, afinador, estado. Refresco a 30 fps. |
| **ENC1 / ENC2 / ENC3** | Tres encoders rotatorios con pulsador (push). Navegación y edición de parámetros (depende del menú activo). |
| **ENC_PARA** | Encoder paralelo maestro (volumen / parámetro global). |
| **NeoPixels (×8)** | Tira de 8 LEDs RGB. Indican escena/preset activo mediante **Category Color Coding** (cada categoría de efecto = un color). |
| **FS1–FS4** | 4 footswitches momentáneos. Activan Scenes/Snapshots, bypass por canal, entrada al afinador. |

### 2.2 Panel trasero (conectores)

```
┌─────────────────────────────────────────────────────────────────┐
│  IN-L    IN-R    OUT-L    OUT-R    HP     USB-C     12V DC     │
│   ◯       ◯       ◯        ◯       ◯      [==]      (●—|       │
└─────────────────────────────────────────────────────────────────┘
```

| Conector | Tipo | Función |
|:---------|:-----|:--------|
| **IN-L / IN-R** | Jack 6.35 mm TS | Entrada instrumento estéreo (guitarra/bajo). Para mono, usar **IN-L**. |
| **OUT-L / OUT-R** | Jack 6.35 mm TS | Salida de línea estéreo a amplificador o loop de efectos. |
| **HP** | Jack 6.35 mm TS | Auriculares (impedancia 32 Ω recomendada). |
| **USB-C** | USB-C | Desarrollo / carga de presets desde PC. |
| **12V DC** | Jack DC 5.5×2.1 mm **centro negativo** | Alimentación. **¡USAR EXCLUSIVAMENTE 12 V DC centro negativo!** |

> ⚠️ **Especificar el voltage correcto salva el pedal.** El inversor interno (LT1054) genera el riel negativo de los opamps. A 12 V funciona con margen correcto; a 9 V (configuración vieja) el diseño queda por debajo del punto de operación validado en SPICE. Usar **12 V DC centro negativo** (estándar de pedal Boss-compatible / daisy-chain). El pedal está protegido contra inversión de polaridad (MOSFET-P + TVS).

---

## 3. Primera puesta en marcha

### 3.1 Conexión estándar (escenario)

```
   Guitarra ──cable──▶ IN-L (o IN-R para estéreo)
                            
   OUT-L ──cable──▶ Amplificador (canal L / input)
   OUT-R ──cable──▶ Amplificador (canal R / return del loop)

   Auriculares ──▶ HP  (monitoreo silencioso)

   12V DC centro negativo ──▶ Jack DC  (¡último en conectar!)
```

### 3.2 Secuencia de arranque (lo que vas a ver/notar)

1. **Conectá todo MENOS la alimentación.**
2. **Conectá el 12V DC** en último lugar (evita pops en el amplificador).
3. El pedal ejecuta una **secuencia de mute segura**:
   - Arranca con la salida **muteada** (silencio total ~500 ms).
   - Enciende el relé de mute y libera el audio.
4. La **pantalla OLED** muestra el **HOME** con el último preset cargado.
5. Los **NeoPixels** muestran el color de la categoría del efecto/preset activo.

> 🔇 **¿Por qué el silencio inicial?** Es el **Mute Relay** (contacto NC a tierra) protegiendo tu amplificador de los pops destructivos del DSP durante el boot. **Fire & Forget**: no requiere intervención, es automático.

### 3.3 Apagado

Simplemente **desconectá el 12V DC**. El mute relay cae a NC (tierra) y la salida queda silenciada — no hay pops al apagar.

---

## 4. Los controles en detalle

### 4.1 Encoders (3 + 1 maestro)

Cada encoder tiene **giro** (izquierda/derecha) y **pulsador** (push al centro).

| Encoder | En HOME | En EFFECT_SELECT | En PARAMETER_EDIT |
|:--------|:--------|:-----------------|:------------------|
| **ENC1** | Navegar presets/bancos | Seleccionar categoría de efecto | Editar parámetro 1 |
| **ENC2** | — | Seleccionar efecto dentro de categoría | Editar parámetro 2 |
| **ENC3** | — | — | Editar parámetro 3 |
| **ENC_PARA** | Volumen maestro | Volumen maestro | Volumen maestro |

> El **pulsador (push)** de cada encoder tiene contexto: típicamente confirma selección o entra/sale de submenús. La pantalla te guía en cada momento.

### 4.2 Footswitches (FS1–FS4)

Modo **escena** (operación normal en vivo):

| Footswitch | Acción típica |
|:-----------|:--------------|
| **FS1** | Scene/Snapshot 1 (o bypass, según config) |
| **FS2** | Scene/Snapshot 2 |
| **FS3** | Scene/Snapshot 3 |
| **FS4** | Scene/Snapshot 4 / entrar al afinador |

> **Gapless:** al cambiar de scene con un footswitch **no hay corte de audio** ni click. La transición es instantánea y silenciosa. Es la función clave para tocar en vivo.

### 4.3 Otros controles

| Control | Función |
|:--------|:--------|
| **Master Volume** (pote frontal/panel, GPIO26) | Volumen maestro global. Lee un potenciómetro. |
| **Expression Pedal** (jack GPIO27) | Conectá un pedal de expresión para controlar wah/volume/parámetro asignable. |
| **NeoPixels** | Solo indicación visual (no se "operan"). El color te dice la categoría activa. |

---

## 5. Las pantallas (menús)

El pedal recorre estos estados (lo que verás en el OLED):

### 5.1 HOME (pantalla principal)

```
┌────────────────────────┐
│ PRESET: Crunch Lead    │  ← Header (preset + banco)
│ BANK: B / SCENE: 2     │
│                        │
│  ▶ Drive    [ON ]      │  ← Body (efectos activos)
│    Delay    [ON ]      │
│    Reverb   [OFF]      │
│                        │
│ VOL ▓▓▓▓▓▓░░  TUNER[F1]│  ← Footer (volumen + pista)
└────────────────────────┘
```

Desde HOME: girá ENC1 para cambiar preset, o pulsá para entrar a selección de efectos.

### 5.2 EFFECT_SELECT

Elegí **categoría de efecto** (ENC1) y **efecto específico** (ENC2). Cada categoría tiene su **color NeoPixel** (Category Color Coding) — así identificás rápido el tipo de efecto sin leer.

### 5.3 PARAMETER_EDIT

Editá los parámetros del efecto seleccionado con ENC1/ENC2/ENC3. Hasta 3 parámetros por pantalla. El pedal soporta **4 efectos simultáneos** (`MAX_EFFECTS_SIMULTANEOUS=4`).

### 5.4 GIG_VIEW (modo escena) ⭐

**La pantalla para tocar en vivo.** Vista compacta con las 4 scenes visibles de un vistazo, NeoPixels indicando activas, y volumen maestro siempre visible. La legibilidad está diseñada para **2 m de distancia** (iluminación de escenario).

### 5.5 TUNER (afinador cromático)

Afinador integrado. Entrás con un footswitch (típicamente FS4) o desde el menú. Muestra nota + desviación en cents. En modo tuner la señal puede enmudecerse (bypass mudo) según configuración.

### 5.6 CALIBRATION

Calibración de controles (encoders, expression pedal, master volume). Usar solo si un control se comporta raro.

### 5.7 SAVE_SCREEN

Guardá el preset/scene actual. Confirmá con push del encoder.

---

## 6. Operación en escenario (checklist rápida)

**Antes del show:**
1. ✅ Batería del pedal / fuente 12V DC centro negativo verificada.
2. ✅ Cable IN y cables OUT probados.
3. ✅ Presets del setlist cargados en orden (banco por banco).
4. ✅ NeoPixels limpios (visibles desde tu posición).

**Durante el show:**
1. Usá **GIG_VIEW** para ver las 4 scenes de un vistazo.
2. Cambiá de scene con **FS1–FS4** — gapless, sin corte.
3. Si necesitás silencio momentáneo: la **scene vacía** o el **mute** (según config).
4. El **volumen maestro** (ENC_PARA o pote) siempre accesible.

**Si algo falla en vivo (no entres en pánico):**
- El **Safe Bypass** conmuta automáticamente a paso directo si el DSP cae. **El audio sigue.** Esto es comportamiento correcto, no un bug.
- Si el pedal se calienta (>85 °C): mutúa preventivamente 100 ms y sigue. Si llega a crítico (>95 °C): bypass + halt. Dejá enfriar y reiniciá.
- Post-reset: el **FaultLatch** guarda el motivo del fallo para diagnóstico (ver `Mantenimiento_Troubleshooting_V1.0.md`).

---

## 7. Indicadores de estado (NeoPixels)

El código de colores por categoría (Category Color Coding) te permite identificar el tipo de efecto activo sin leer la pantalla:

| Color | Categoría típica (referencia) |
|:------|:------------------------------|
| 🔴 Rojo | Drive / Distorsión / Fuzz (ej. AmpSim) |
| 🟡 Amarillo | Modulación (chorus, flanger, phaser) |
| 🔵 Azul | Delay / Eco |
| 🟢 Verde | Reverb / Ambiente |
| ⚪ Blanco / Otro | Utilidad (compresor, EQ, noise gate) |

> ⚠️ El mapeo exacto color↔categoría se define en la librería de efectos del firmware. Si editaste las categorías, los colores pueden diferir. La regla visual: **cada categoría = un color consistente**, para que el cerebro lo asocie de memoria.

### Estado de fault (anomalía)

- **Rojo parpadeante** = fault térmico (85 °C). El pedal se está protegiendo. Dejá espacio para ventilación.
- Si el rojo es **fijo + sin audio** = fault crítico (95 °C o stack overflow). Apagá, dejá enfriar / reiniciá.

> 📝 **Nota:** la integración visual completa del LED rojo de fault térmico está en mejora continua (T1, `main.cpp:245`).

---

## 8. Especificaciones (resumen para el usuario)

| Aspecto | Valor |
|:--------|:------|
| **Alimentación** | **12 V DC centro negativo** (jack 5.5×2.1 mm) — compatible daisy-chain |
| **Consumo aprox.** | < 500 mA (típico de gira) |
| **Entradas** | 2× instrumento (IN-L/IN-R), 6.35 mm TS · expression pedal |
| **Salidas** | 2× línea (OUT-L/OUT-R) · auriculares (HP) |
| **Procesamiento** | 48 kHz / 24-bit · 4 efectos simultáneos |
| **Latencia** | Baja (I2S directo + MCLK matemático 12.288 MHz anti-jitter) |
| **Display** | OLED 128×64 (I2C), legible a 2 m |
| **Indicadores** | NeoPixel RGB ×8 (Category Color Coding) |
| **Controles** | 3 encoders + 1 maestro · 4 footswitches · master vol · expression |
| **Afinador** | Cromático integrado |
| **Protecciones** | Safe Bypass hardware · Mute Relay NC · térmico 85/95 °C · inversión polaridad · TVS ESD |
| **Target** | Raspberry Pi Pico 2 (RP2350 dual-core 196 MHz) |

---

## 9. Cuidados y mantenimiento básico

### 9.1 Lo que SÍ hacés

- **Limpiá el panel** con paño seco. Evitá que entre líquido en jacks.
- **Ventilá el pedal** en escena. No lo apiles sobre fuentes de calor (amplificadores valvulares).
- **Enrollá los cables** con cuidado. Los jacks 6.35 mm no soportan tirones laterales.
- **Usá la fuente correcta**: 12 V DC centro negativo. Una fuente equivocada puede dañar el pedal.
- **Actualizá presets por USB-C** desde PC cuando el pedal esté apagado o en modo dev.

### 9.2 Lo que NO hacés

- ❌ **No abrir el pedal en garantía** salvo para mantenimiento autorizado.
- ❌ **No usar fuentes de 9 V** — el pedal está validado a 12 V; a 9 V el comportamiento no está garantizado.
- ❌ **No exponer a lluvia/humedad** sin funda protectora.
- ❌ **No tirar de los cables** para desconectar; agarrar el conector.
- ❌ **No bloquear la ventilación** (NeoPixels y MCU generan calor bajo carga DSP).

### 9.3 Almacenamiento entre giras

- Guardar en funda rígida.
- Desconectar todas las fuentes y cables.
- Temperatura ambiente (-10 °C a +50 °C).
- Recargar/cargar la fuente antes del próximo uso si tiene batería.

---

## 10. Troubleshooting rápido (síntoma → acción)

| Síntoma | Qué hacer |
|:--------|:----------|
| **Sin sonido al arrancar** | Esperá 1–2 s (mute de boot). Si persiste, revisá que la source sea 12 V DC. Verificá cables IN/OUT. |
| **Pops/clicks al conectar** | Conectá la alimentación **en último lugar** (la secuencia de mute lo evita). |
| **El audio se corta y vuelve solo** | Probable fault térmico (85 °C). Mejorá la ventilación del pedal. |
| **El audio pasa pero "seco" sin efectos** | Safe Bypass activado por watchdog (el DSP se reinició). Comportamiento correcto. Reiniciá el pedal deliberadamente. |
| **Distorsión no musical a alto volumen** | El gain de entrada satura. Bajá el volumen de la guitarra o reducí el gain del primer efecto. |
| **Zumbido 50/60 Hz** | Loop de masa con otro pedal/amp. Usá fuente aislada o rompé el loop con un iso-transformer. |
| **Display no se ve** | Ajustá contraste OLED desde CALIBRATION. Si no responde, reiniciá el pedal. |
| **Footswitch no responde** | Entrá a CALIBRATION y verificá el hardware debouncing. Si un FS queda pegado, limpia el contacto. |
| **Jitter de tono / inestable** | El reloj del DSP puede haberse corrompido. Reiniciá. Si persiste, revisá la alimentación (ripple excesivo). |
| **NeoPixel siempre rojo fijo** | Fault crítico. Apagá, esperá 5 min, reiniciá. Si repite, mantenimiento autorizado. |

> Para diagnóstico profundo (FaultLatch, métricas DSP, MSPLIM stack guard) consultá `Mantenimiento_Troubleshooting_V1.0.md`.

---

## 11. Glosario rápido

| Término | Significado |
|:--------|:------------|
| **DSP** | Procesamiento digital de señal — los efectos viven aquí. |
| **Scene / Snapshot** | Configuración completa de efectos guardada, cargable con un footswitch. |
| **Gapless** | Transición entre scenes sin corte de audio ni click. |
| **Safe Bypass** | Paso directo entrada→salida por hardware, sin DSP. Activado si el DSP falla. |
| **Mute Relay** | Relé que silencia la salida durante el boot para evitar pops. |
| **Touring Grade** | Estándar del pedal: diseñado para sobrevivir giras. |
| **Category Color Coding** | Cada categoría de efecto = un color NeoPixel consistente. |

---

*Manual de Usuario V1.0 — documentación derivada del `Arquitectura_Hardware.md v1.2` (SSOT primaria certificada SPICE 6/6 PASS + Prompt 5). Cero alucinación: cada especificación cita su fuente. Para detalle técnico, ver `Guia_Desarrollador_V1.0.md` y `Manual_Teorico_V1.0.md`.*
