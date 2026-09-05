#pragma once

#include <Arduino.h>
#include <atomic>

/**
 * @brief Afinador Cromático de alta precisión para RP2350.
 *
 * Utiliza un algoritmo de Autocorrelación de bajo consumo (YIN-like light)
 * operando en buffers de 1024 muestras para detectar frecuencias de guitarra.
 *
 * RT-Safety (P1): el AMDF (~500k ops enteras por ventana) NUNCA corre en el
 * hot-path de audio. Core 0 solo captura (O(1) por muestra) en ping-pong
 * atómico (rules/3 §3.10.1); Core 1 consume la ventana publicada vía
 * processAnalysisCore1() — mismo patrón de offloading que
 * sweetSpot.processAsyncCalibrationCore1() en loop1().
 */
class Tuner {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    Tuner();
    void init(float sampleRate);

    /**
     * @brief Captura una muestra para detección (Core 0, hot-path — solo 1 store + índice).
     * @param sample Muestra en punto flotante [-1.0, 1.0].
     */
    void processSample(float sample);

    /**
     * @brief Análisis AMDF diferido (Core 1 — llamar desde loop1()).
     * Consume la ventana publicada por el ping-pong atómico, si la hay.
     */
    void processAnalysisCore1();

    /**
     * @brief Obtiene la nota detectada (para la UI).
     * @return Puntero a string con la nota (ej. "A", "G#").
     */
    const char* getNoteName();

    /**
     * @brief Obtiene la desviación en cents (-50 a +50).
     */
    int8_t getCents();

    /**
     * @brief Indica si hay una señal válida para afinar.
     */
    bool hasSignal() const { return signalActive; }

private:
    float sampleRate_;

    // Ping-pong de 2 ventanas (Lock-Free IPC, regla 3.10.1 — cero locks):
    //   buffers[writeSlot] : propiedad exclusiva de Core 0 (captura).
    //   readySlot != SLOT_NONE : ventana llena publicada, propiedad de Core 1.
    //   Transferencia NONE->idx (Core 0) / idx->NONE (Core 1) vía std::atomic.
    static constexpr uint16_t BUFFER_SIZE = 1024;
    static constexpr uint8_t  SLOT_NONE   = 255;
    int16_t buffers[2][BUFFER_SIZE];
    uint8_t  writeSlot;   // slot de captura actual (solo Core 0)
    uint16_t writeIdx;    // posición dentro de buffers[writeSlot]
    std::atomic<uint8_t> readySlot;

    // Variables de salida atómicas (análisis -> UI; hoy ambas en Core 1,
    // se conservan atómicas por si la UI migra de core)
    std::atomic<uint8_t> detectedNoteIdx;
    std::atomic<int8_t> centsOffset;
    std::atomic<bool> signalActive;

    // Frecuencias de referencia (A4 = 440Hz)
    static const float noteFrequencies[12];
    static const char* noteNames[12];

    void performAnalysis(const int16_t* window);
};
