#pragma once

#include <Arduino.h>
#include <I2S.h>
#include "EffectSlots.h"
#include "Tuner.h"
#include "../config/ConfigManager.h"
#include "ControlMatrix.h" // Dependencia para InputLevel

// ==========================================
// BUFFERS I2S CONFIGURABLES (REV4 - FASE 4: Latencia Boutique)
// ==========================================
// FASE 4: Reducido a 2x32 para latencia boutique (~1.3ms vs ~2ms)
// 2x32 = 64 muestras = 1.33ms @ 48kHz (mantiene estabilidad a 196MHz)
constexpr uint8_t I2S_BUFFER_COUNT = 2;  // Reducido de 3
constexpr uint8_t I2S_BUFFER_SIZE = 32;

class AudioSystemI2S {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    AudioSystemI2S();

    /**
     * @brief Inicializa el sistema de audio I2S DMA.
     *
     * Configura pines, buffers, y frecuencias para los conversores DAC/ADC externos.
     * @return true si la inicialización es exitosa, false si falla.
     */
    bool init();

    /**
     * @brief Establece el gestor de configuración (para acceso a sensibilidad global)
     */
    void setConfigManager(ConfigManager* cm) { configManager = cm; }

    /**
     * @brief Establece el Master BPM global y lo propaga a los efectos.
     */
    void setMasterBPM(float bpm) { slots.setGlobalTempoBpm(bpm); }

    /**
     * @brief Conecta el detector de envolvente para inyectarle audio en el hot-path
     */
    void setInputLevel(InputLevel* il) { inputLevel = il; }

    /**
     * @brief Actualiza la caché asíncrona de sensibilidad global (Llamar desde UI o al init)
     */
    void updateGlobalSensitivity();

    /**
     * @brief Procesa el audio en tiempo real de forma NO bloqueante.
     */
    void processAudio(); // Renombrado de processPassthrough ya que ahora tiene efectos

    /**
     * @brief Permite acceso al motor de efectos (Para UI).
     */
    EffectSlots& getEffectSlots() { return slots; }

    Tuner& getTuner() { return tuner; }

    /**
     * @brief Obtiene el pico máximo actual de señal (0-8388607).
     * @param reset Si es true, reinicia el contador tras la lectura.
     */
    int32_t getPeakL(bool reset = true) { return peakL.exchange(reset ? 0 : peakL.load(std::memory_order_relaxed), std::memory_order_relaxed); }
    int32_t getPeakR(bool reset = true) { return peakR.exchange(reset ? 0 : peakR.load(std::memory_order_relaxed), std::memory_order_relaxed); }

    /**
     * @brief Obtiene el contador de underruns I2S (métrica de estabilidad).
     * @return Número total de underruns desde el inicio.
     */
    volatile uint32_t& getUnderrunCount() { return i2sUnderrunCount; }

    /**
     * @brief Obtiene la frecuencia de muestreo actual
     * @return Frecuencia en Hz ((int)globalSampleRate Hz para PCM1808/PCM5102)
     */
    float getSampleRate() const { return globalSampleRate; }

private:
    I2S i2sIn;
    I2S i2sOut;
    EffectSlots slots;
    Tuner tuner;
    ConfigManager* configManager;  // Puntero a gestor de configuración
    InputLevel* inputLevel = nullptr; // Puntero a envolvente

    std::atomic<int32_t> peakL;
    std::atomic<int32_t> peakR;
    volatile uint32_t i2sUnderrunCount;  // Métrica de estabilidad (REV4)

    std::atomic<float> targetGlobalSensitivity{1.0f}; // Target para transición suave
    float currentGlobalSensitivity{1.0f}; // Caché de sensibilidad global (solo hilo de audio)

    // FASE 4: Estado para TPDF Dither (lock-free)
    static uint32_t ditherSeed;

    // FASE 4: Función helper para TPDF Dither (inline ultra rápido)
    inline int32_t applyTPDFDither(int32_t sample);

    // DÍA 1: Infraestructura Crítica - CPU Hard-Mute
    std::atomic<bool> cpuLimitActive{false};
    uint32_t getCycleCount() {
        return *(volatile uint32_t*)0xE000E018; // SYST_CVR (SysTick Current Value)
    }
};
