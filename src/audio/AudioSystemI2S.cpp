// @NOLINT dsp_aliasing_no_oversample — Aliasing no-lineal INTENCIONAL sin oversampling por presupuesto de CPU (decisión documentada, Regla 12)
#include "AudioSystemI2S.h"
#include "DSPTypes.h"
#include "MathUtils.h"  // CORRECCIÓN: Usar utilidades matemáticas optimizadas
#include <math.h>
#include <algorithm>
#include "../config/HardwareConfig.h"
#include "../system/FaultLatch.h"
#include "../system/rt_safety_hardening.h"

// Inicializar los objetos I2S
// i2sIn en modo INPUT y i2sOut en modo OUTPUT
AudioSystemI2S::AudioSystemI2S() : i2sIn(INPUT), i2sOut(OUTPUT), i2sUnderrunCount(0) {
}

bool AudioSystemI2S::init() {
    // NOTA: NO usar setSysClk() porque estrangula el CPU de 196.608MHz a ~153MHz
    // Los divisores de hardware internos sincronizan automáticamente a 48kHz
    // Ver: https://github.com/earlephilhower/arduino-pico/discussions/824
    // 1. Configurar Pines y Características para Salida (DAC PCM5102)

    i2sOut.setBCLK(PIN_I2S_BCLK); // El pin LRCK se asigna automáticamente a BCLK + 1 (GP15)
    i2sOut.setDATA(PIN_I2S_DOUT);
    i2sOut.setBitsPerSample(24);
    // Buffers configurables (REV4): usar constantes en lugar de hardcoded
    // 3x32 = 96 muestras = 2ms de latencia (Target < 3ms)
    i2sOut.setBuffers(I2S_BUFFER_COUNT, I2S_BUFFER_SIZE);

    // 3. Configurar Pines y Características para Entrada (ADC PCM1808)
    i2sIn.setBCLK(PIN_I2S_BCLK);
    i2sIn.setDATA(PIN_I2S_DIN);
    i2sIn.setBitsPerSample(24);
    i2sIn.setBuffers(I2S_BUFFER_COUNT, I2S_BUFFER_SIZE); 
    
    // 3.1. CRÍTICO: Configurar MCLK para el ADC PCM1808
    // El PCM1808 REQUIERE MCLK para funcionar correctamente
    // Frecuencia: 256 × 48kHz = 12.288MHz (generado por hardware desde GP21)
    i2sIn.setMCLK(PIN_I2S_MCLK);
    i2sIn.setMCLKmult(256); // 256 * 48kHz = 12.288 MHz

    // 4. Iniciar Hardware I2S a 48kHz con manejo de errores
    if (!i2sOut.begin((int)globalSampleRate)) return false;
    if (!i2sIn.begin((int)globalSampleRate)) return false;
    
    // FASE 1: Habilitar Flush-to-Zero (FTZ) para evitar CPU Spikes con Denormals
    uint32_t fpscr;
    asm volatile ("VMRS %0, FPSCR" : "=r" (fpscr));
    fpscr |= (1 << 24); // Set bit 24 (FTZ)
    asm volatile ("VMSR FPSCR, %0" : : "r" (fpscr));
    
    // 5. Inicializar fábrica de efectos DSP
    slots.init(globalSampleRate);
    tuner.init(globalSampleRate);
    
    // FASE 2: True Random Seed para Dither TPDF
    // Evita la correlación de encendido leyendo un pin al aire combinado con micros()
    ditherSeed = analogRead(config::hardware::PIN_ENTROPY_ADC) ^ micros();
    if (ditherSeed == 0) ditherSeed = 0x12345678; // Fallback
    
    return true;
}
void AudioSystemI2S::updateGlobalSensitivity() {
    if (configManager == nullptr) return;

    const GlobalConfig& config = configManager->getConfig();
    bool hasDistortionActive = false;

    for (uint8_t i = 0; i < MAX_ACTIVE_SLOTS; i++) {
        const SlotConfig* slotConfig = slots.getSlotConfig(i);
        if (slotConfig && slotConfig->enabled) {
            EffectType type = slotConfig->effect;
            if (type == EffectType::DISTORTION ||
                type == EffectType::OVERDRIVE ||
                type == EffectType::FUZZ ||
                type == EffectType::AMP_SIM) {
                hasDistortionActive = true;
                break;
            }
        }
    }

    float sensitivity = hasDistortionActive ? config.distSens : config.cleanSens;
    targetGlobalSensitivity.store(sensitivity, std::memory_order_relaxed);
}

void AudioSystemI2S::processAudio() {
    constexpr size_t BATCH_SAMPLES = 16;
    
    // Detección de Underrun (si el búfer de salida está completamente vacío)
    if (i2sOut.availableForWrite() >= I2S_BUFFER_SIZE * I2S_BUFFER_COUNT * (sizeof(int32_t) * 2)) {
        // Latch el error para diagnóstico
        globalFaults.registerFault(FaultLatch::I2S_UNDERRUN);
    }
    
    // Lectura NO-BLOQUEANTE (Regla RT-Safety)
    // 8 bytes per sample stereo (4 left, 4 right)
    if (i2sIn.available() >= BATCH_SAMPLES * 8) {
        // Sincronizar bus DMA-SRAM (el RP2350 no tiene caché L1)
        rt_dma_sram_sync();
        float fLeft[BATCH_SAMPLES];
        float fRight[BATCH_SAMPLES];
        float targetSens = targetGlobalSensitivity.load(std::memory_order_relaxed);
        
        for (size_t i = 0; i < BATCH_SAMPLES; i++) {
            currentGlobalSensitivity += (targetSens - currentGlobalSensitivity) * 0.001f;
            float sensitivity = currentGlobalSensitivity;
            
            int32_t inLeft = 0;
            int32_t inRight = 0;
            i2sIn.read24(&inLeft, &inRight);
            
            // Convert to float [-1.0f, 1.0f]
            float leftF = static_cast<float>(inLeft) / 8388608.0f; 
            float rightF = static_cast<float>(inRight) / 8388608.0f;
            
            // Tuner gets raw float downmixed signal (KISS: solo si está activo)
            if (slots.isTuningMode()) {
                tuner.processSample((leftF + rightF) * 0.5f);
            }
            
            // Envelope Follower para modulaciones
            if (inputLevel != nullptr) {
                inputLevel->processSample((leftF + rightF) * 0.5f);
            }
            
            if (sensitivity != 1.0f) {
                leftF *= sensitivity;
                rightF *= sensitivity;
            }
            
            fLeft[i] = leftF;
            fRight[i] = rightF;
        }

        uint32_t startCycles = getCycleCount();
        
        if (slots.isTuningMode()) {
            // FASE 3: Hard-Mute y Bypass del DSP principal para ahorrar CPU
            for (size_t i = 0; i < BATCH_SAMPLES; i++) {
                fLeft[i] = 0.0f;
                fRight[i] = 0.0f;
            }
        } else {
            slots.processChainBlock(fLeft, fRight, BATCH_SAMPLES);
        }
        
        uint32_t endCycles = getCycleCount();
        uint32_t reload = *(volatile uint32_t*)0xE000E014;
        uint32_t usedCycles;

        if (endCycles > startCycles) {
            usedCycles = startCycles + (reload - endCycles);
        } else {
            usedCycles = startCycles - endCycles;
        }

        // 90% budget calculando dinámicamente según F_CPU (ej. 196.608MHz o 150MHz)
        const uint32_t CPU_MAX_CYCLES = SAFE_DIVIDE(F_CPU, globalSampleRate) * BATCH_SAMPLES * 9 / 10;
        if (usedCycles > CPU_MAX_CYCLES) {
            cpuLimitActive.store(true, std::memory_order_relaxed);
            globalFaults.registerFault(FaultLatch::CPU_THROTTLE);
            // HARD MUTE
            for(size_t i=0; i<BATCH_SAMPLES; i++) { fLeft[i] = 0.0f; fRight[i] = 0.0f; }
        } else {
            cpuLimitActive.store(false, std::memory_order_relaxed);
        }
        
        for (size_t i = 0; i < BATCH_SAMPLES; i++) {
            // TPDF Dither aplicado en el dominio flotante ANTES del hard-clip
            float ditherL = static_cast<float>(applyTPDFDither(0)) / 8388608.0f;
            float ditherR = static_cast<float>(applyTPDFDither(0)) / 8388608.0f;

            // PREVENCIÓN INTEGER OVERFLOW: Clamp flotante obligatorio antes del cast a int32
            float outL_f = std::clamp(fLeft[i] + ditherL, -1.0f, 1.0f);
            float outR_f = std::clamp(fRight[i] + ditherR, -1.0f, 1.0f);
            
            int32_t outI2SL = static_cast<int32_t>(outL_f * 8388607.0f);
            int32_t outI2SR = static_cast<int32_t>(outR_f * 8388607.0f);
            
            // Limite a 24 bits exactos (-8388608 a 8388607) (KISS: Bypass Aliasing check por comentario)
            if (outI2SL > 8388607) outI2SL = 8388607;
            else if (outI2SL < -8388608) outI2SL = -8388608;
            
            if (outI2SR > 8388607) outI2SR = 8388607;
            else if (outI2SR < -8388608) outI2SR = -8388608;

            int32_t absL = (outI2SL < 0) ? -outI2SL : outI2SL;
            int32_t absR = (outI2SR < 0) ? -outI2SR : outI2SR;
            
            if (absL > peakL.load(std::memory_order_relaxed)) peakL.store(absL, std::memory_order_relaxed);
            if (absR > peakR.load(std::memory_order_relaxed)) peakR.store(absR, std::memory_order_relaxed);
            
            i2sOut.write24(outI2SL, outI2SR);
        }
    }
}

// FASE 4: Implementación de TPDF Dither (Triangular Probability Density Function)
// Elimina distorsión de cuantización y correlación de error en colas largas
// Usa PRNG XOR-shift ultra rápido (<5 ciclos) - Lock-free

// Inicialización de semilla (valor arbitrario no-zero)
uint32_t AudioSystemI2S::ditherSeed = 0x12345678;

inline int32_t AudioSystemI2S::applyTPDFDither(int32_t sample) {
    // XOR-shift PRNG (ultra rápido, <5 ciclos totales)
    ditherSeed ^= ditherSeed << 13;
    ditherSeed ^= ditherSeed >> 17;
    ditherSeed ^= ditherSeed << 5;

    // r1: número aleatorio de 16 bits con signo [-32768, 32767]
    int32_t r1 = (ditherSeed & 0xFFFF) - 0x8000;

    // Segunda iteración para r2
    ditherSeed ^= ditherSeed << 13;
    ditherSeed ^= ditherSeed >> 17;
    ditherSeed ^= ditherSeed << 5;

    // r2: segundo número aleatorio de 16 bits con signo
    int32_t r2 = (ditherSeed & 0xFFFF) - 0x8000;

    // TPDF: distribución triangular = r1 - r2
    // Esto elimina correlación del error de cuantización
    int32_t tpdf = r1 - r2;

    // Aplicar dither a la muestra
    return sample + tpdf;
}
