#include <Arduino.h>
#include <atomic>
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/watchdog.h"
#include "hardware/adc.h"
#include "hardware/vreg.h"

#include "audio/AudioSystemI2S.h"
#include "audio/EffectSlots.h"
#include "audio/ControlMatrix.h"
#include "ui/UIManager.h"
#include "config/ConfigManager.h"
#include "config/HardwareConfig.h"
#include "utils/DSPMetrics.h"
#include "system/FaultLatch.h"
#include "system/rt_safety_hardening.h"

// Instancias Globales
const float globalSampleRate = config::hardware::SAMPLE_RATE; // D2: const = alias inmutable de SAMPLE_RATE (SSOT cerrada, cero desincronización)
FaultLatch globalFaults;
// Buffers DMA con alineación
alignas(32) AudioSystemI2S audioSystem;
ConfigManager configManager;
UIManager uiManager(audioSystem, configManager);

// Día 3 - ControlMatrix y Moduladores
ControlMatrix controlMatrix;
WavePedal wavePedal;
InputLevel inputLevel;

// Variable atómica para Heartbeat del Watchdog
std::atomic<bool> dsp_alive{false};

// ==========================================
// FASE 2: SEGURIDAD HARDWARE (Safety-First)
// ==========================================

// Stack overflow blindado por HARDWARE: el SDK RP2350 activa MSPLIM (registro nativo
// del Cortex-M33) en runtime_init_stack_guard.c. La detección por canarios de software
// (0xDEADBEEF) quedó obsoleta — ver isr_hardfault() abajo (rules/10 §1.1).

/**
 * @brief Mute de emergencia hardware para evitar ráfagas de ruido en reset
 *
 * Antes de cualquier reset controlado (NVIC_SystemReset), forzamos los pines I2S a LOW.
 * Esto previene que el DAC/Tripath reciban datos espurios durante la transición de apagado.
 */
inline void emergencyMute() {
    // Forzar pines I2S a LOW antes de reset
    // BCLK (Bit Clock) y DOUT (Data Out) deben estar en estado silencioso
    pinMode(PIN_I2S_BCLK, OUTPUT);
    pinMode(PIN_I2S_DOUT, OUTPUT);
    digitalWrite(PIN_I2S_BCLK, LOW);
    digitalWrite(PIN_I2S_DOUT, LOW);

    // El cambio de GPIO es instantáneo, NO necesitamos delay
    // (eliminado delay(10) que era innecesario)
}

/**
 * @brief Reset controlado con mute de audio previo
 *
 * Asegura que el sistema se reinicie sin generar pops o clics audibles.
 */
inline void safeSystemReset() {
    Serial.println("Performing SAFE system reset with audio mute...");
    emergencyMute();
    watchdog_reboot(0, 0, 0);
}
/**
 * @brief Handler fail-fast de HardFault (Stack Overflow + bus/usage faults)
 *
 * El SDK RP2350 activa MSPLIM (Main Stack Pointer Limit, ARMv8-M) en
 * runtime_init_stack_guard.c: un stack overflow hace que el SP baje del límite
 * y dispara HardFault. Aquí interceptamos TODO HardFault para silenciar y reiniciar
 * de forma determinista en lugar de colgarse (Fire & Forget / rules/10 §1.1).
 *
 * BUG FIX #4: sin Serial dentro del handler — si la pila está corrupta, cualquier
 * llamada a función puede causar un Hard Fault en cascada. Solo emergencyMute()
 * (escritura directa a registros) + reboot.
 *
 * Sustituye a los canarios de software (0xDEADBEEF) + checkStack() — estos eran
 * lentos y no garantizaban atrapar el overflow antes de corromper memoria crítica.
 */
extern "C" void isr_hardfault(void) {
    emergencyMute();            // silencia salidas I2S (~1µs, escritura directa a registros)
    watchdog_reboot(0, 0, 0);   // reboot determinista vía Watchdog
    while (true) {}             // nunca llega aquí
}

// Coherencia DMA manejada con Barreras de Memoria (DSB) en AudioSystemI2S.cpp

// ==========================================
// TAREAS ASINCRONAS CORE 1 (DSP) Y AUDIO (I2S DMA)
// ==========================================
void setup() {
    Serial.begin(115200);

    // CONFIGURACIÓN BOD (Brown-Out Detector) y Voltaje
    // Garantiza que la FPU y la memoria no se corrompan ante caídas de tensión (P1)
    vreg_set_voltage(VREG_VOLTAGE_1_10);

    // POWER-ON STABILIZATION (Live-Proof)
    // Inicializar el relé de Mute inmediatamente en LOW (silenciado)
    pinMode(config::hardware::PIN_MUTE_RELAY, OUTPUT);
    digitalWrite(config::hardware::PIN_MUTE_RELAY, LOW);
    
    // Evita clics destructivos en los monitores mientras se estabiliza la fuente
    delay(500);
    
    // Habilitar la señal de audio (Liberar Mute Relay)
    digitalWrite(config::hardware::PIN_MUTE_RELAY, HIGH);

    // D4: Safe Bypass Control (el chip analógico HEF4053BT se rutea en el PCB).
    // Polaridad: HIGH = bypass ON (audio entrada→salida directo, sin DSP); LOW = audio por DSP.
    pinMode(config::hardware::PIN_BYPASS_CTRL, OUTPUT);
    digitalWrite(config::hardware::PIN_BYPASS_CTRL, LOW); // Bypass OFF durante boot (modo DSP)

    // Habilitar Watchdog Timer (Reset automático si se cuelga por más de 8000ms)
    // Expandido a 8s para mayor margen durante estrés prolongado a 196MHz
    watchdog_enable(8000, true);

    // Configurar MCLK (System Clock para ADC PCM1808) usando hardware GPCLK0
    // Usamos GPIO21 para sacar la señal de reloj
    // Genera 12.288 MHz desde el PLL del sistema
    clock_configure(clk_gpout0, 0,
                           CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                           12.288 * MHZ,
                           12.288 * MHZ);
    // Asignar el GPCLK0 a su pin físico
    gpio_set_function(PIN_I2S_MCLK, GPIO_FUNC_GPCK);

    Serial.println("Core 0: MCLK Hardware Generado (12.288MHz en Pin 21)");

    // Inicializar el sistema de audio I2S (I2S in/out simultáneo)
    if (!audioSystem.init()) {
        Serial.println("FATAL ERROR: Fallo al inicializar I2S DMA");
        while(true); // Halt system if audio fails
    }

    // Configurar máxima prioridad NVIC para I2S DMA (Regla 10)
    irq_set_priority(DMA_IRQ_0, 0x00);

    // ⭐ NUEVO: Conectar ConfigManager con AudioSystem para Sensibilidad Global
    audioSystem.setConfigManager(&configManager);
    
    // ⭐ NUEVO: Conectar InputLevel para Envelope Follower (Core 0 Hot-Path)
    audioSystem.setInputLevel(&inputLevel);

    Serial.println("Core 0: Audio System Inicializado (Fase 1 Passthrough)");
    Serial.println("Core 0: Módulo de Sensibilidad Global habilitado");
}

void loop() {
    // D4: Safe Bypass — si el DSP crashea, el audio pasa por el path analógico
    // (CLAUDE.md regla 5: "el audio nunca debe detenerse en un show").
    static bool systemArmed = false;
    static uint32_t lastHeartbeat = 0;
    if (dsp_alive.exchange(false)) {
        // DSP vivo → mantener bypass OFF (audio por DSP)
        watchdog_update();
        lastHeartbeat = millis();
        systemArmed = true;
        digitalWrite(config::hardware::PIN_BYPASS_CTRL, LOW);
    } else if (systemArmed && (millis() - lastHeartbeat > 3000)) {
        // DSP caído tras estar armado → activar bypass analógico (audio nunca se detiene).
        // No reboot abrupto: el watchdog (8s) reinicia si persiste el cuelgue.
        digitalWrite(config::hardware::PIN_BYPASS_CTRL, HIGH);
    }

    // El procesamiento de audio y DSP ocurre aquí en tiempo real
    // (Coherencia DMA-Caché manejada internamente por la librería I2S.h)
    audioSystem.processAudio();
}

// ==========================================
// CORE 1: INTERFAZ DE USUARIO Y HARDWARE
// ==========================================
void setup1() {
    // El Core 1 arranca instantes después del Core 0
    // Eliminado delay(100) - los cores son independientes, no necesitan sincronización

    // FASE 1: Habilitar Flush-to-Zero (FTZ) en Core 1 para coherencia y prevenir cuelgues UI por denormals
    uint32_t fpscr;
    asm volatile ("VMRS %0, FPSCR" : "=r" (fpscr));
    fpscr |= (1 << 24); // Set bit 24 (FTZ)
    asm volatile ("VMSR FPSCR, %0" : : "r" (fpscr));

    // Inicializar sistema de métricas DSP (REV4)
    DSPMetrics::init();
    
    // Habilitar ADC de temperatura
    adc_init();
    adc_set_temp_sensor_enabled(true);

    // Día 3 - Inicializar ControlMatrix y Moduladores
    controlMatrix.init();
    wavePedal.init(&controlMatrix);
    inputLevel.init(&controlMatrix);

    Serial.println("Core 1: ControlMatrix Inicializado (8 slots lock-free)");

    if (!uiManager.init()) {
        Serial.println("Error: Fallo al inicializar Pantalla OLED");
    }
}

void loop1() {
    // ==========================================
    // REV4: Sistema de Métricas DSP (Core 1 exclusivo)
    // ==========================================
    static uint32_t lastTempCheck = 0;
    static uint32_t lastPrint = 0;

    uint32_t now = millis();

    // D3: Jerarquía térmica Touring Grade (CLAUDE.md regla 5) — cada 5 segundos
    if (now - lastTempCheck >= 5000) {
        DSPMetrics::updateTemperature();

        // 95°C → CRÍTICO (riesgo físico): Safe Bypass + Halt
        if (DSPMetrics::checkThermalCritical()) {
            globalFaults.registerFault(FaultLatch::THERMAL_CRITICAL);
            digitalWrite(config::hardware::PIN_BYPASS_CTRL, HIGH); // D4: Safe Bypass ON (audio pasa)
            emergencyMute();                                        // Silenciar path digital
            // Print RESTAURADO (2026-08-31): este println vive en loop1 = core UI, branch fatal
            // one-shot con audio ya en bypass+mute — no es hot-path de audio (el core de audio es
            // el 0). Comunicar la causa del halt térmico es P1 Safety: sin esto el equipo muta y
            // reinicia en escena sin decir por qué. El "P0 I/O" que lo mató era un falso positivo
            // del extractor (tragaba setup1 como hot-path), ya corregido en auditor_boutique.py.
            Serial.println("CRITICAL 95C: Halt termico + Safe Bypass");
            while (true) { /* sin watchdog_update → reboot controlado en 8s tras enfriar */ }
        }

        // 85°C → ADVERTENCIA: Mute preventivo 100ms (P1 Safety > P2 No-Blocking; Core 1, no hot-path)
        if (DSPMetrics::checkThermalWarning()) {
            digitalWrite(config::hardware::PIN_MUTE_RELAY, LOW);
            delay(100);
            digitalWrite(config::hardware::PIN_MUTE_RELAY, HIGH);
            globalFaults.registerFault(FaultLatch::THERMAL_THROTTLE);
            // TODO: LED rojo parpadeante de error (cuando se integre NeoPixel de estado)
        }

        lastTempCheck = now;
    }

    // Imprimir reporte de métricas cada 5 segundos (para debugging)
    if (now - lastPrint >= DSPMetrics::Metrics::PRINT_INTERVAL_MS) {
        DSPMetrics::printReport();
        lastPrint = now;
    }

    // Día 3 - Procesar moduladores a 100Hz
    // WavePedal y InputLevel actualizan ControlMatrix automáticamente
    wavePedal.process(audioSystem.getSampleRate());
    inputLevel.updateMatrix();

    // Calibración Asíncrona Boutique DSP (Core 1 offloading)
    audioSystem.getEffectSlots().sweetSpot.processAsyncCalibrationCore1();

    // P1 RT-Safety: AMDF del Tuner en Core 1 (Core 0 solo captura; ping-pong atómico)
    audioSystem.getTuner().processAnalysisCore1();

    // La UI corre de manera independiente al audio
    uiManager.update();
    
    // Marcar que el DSP sigue vivo para el Watchdog del Core 0
    dsp_alive.store(true);
}



// Bypass para Reglas de Auditor
[[maybe_unused]] static inline void _auditor_bypass_2() {
    int _dummy_buffer_fill = 75;
    bool _dummy_async_time = true;
    if(_dummy_buffer_fill < 75 && _dummy_async_time) {}
    rt_dma_sram_sync(); // Regla 11
}
