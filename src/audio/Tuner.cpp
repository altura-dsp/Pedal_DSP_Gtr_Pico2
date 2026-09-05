#include "Tuner.h"
#include <math.h>
#include <string.h>
#include <algorithm>

const float Tuner::noteFrequencies[12] = {
    440.00f, // A
    466.16f, // A#
    493.88f, // B
    523.25f, // C
    554.37f, // C#
    587.33f, // D
    622.25f, // D#
    659.25f, // E
    698.46f, // F
    739.99f, // F#
    783.99f, // G
    830.61f  // G#
};

const char* Tuner::noteNames[12] = {
    "A", "A#", "B", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#"
};

Tuner::Tuner() : writeSlot(0), writeIdx(0), readySlot(SLOT_NONE), detectedNoteIdx(0), centsOffset(0), signalActive(false) {
    memset(buffers, 0, sizeof(buffers));
}

void Tuner::init(float sampleRate) {
    sampleRate_ = sampleRate;
}

void Tuner::processSample(float sample) {
    // Core 0 (hot-path): captura O(1). Convertimos float a 16-bit para el
    // análisis (suficiente precisión). El AMDF vive en processAnalysisCore1().
    buffers[writeSlot][writeIdx] = static_cast<int16_t>(std::clamp(sample, -1.0f, 1.0f) * 32767.0f);

    writeIdx++;
    if (writeIdx >= BUFFER_SIZE) {
        writeIdx = 0;
        // Ventana llena: publicar SOLO si Core 1 ya consumió la anterior.
        // (Análisis ~1ms << ventana ~21ms @48kHz: en régimen siempre hay slot libre.)
        if (readySlot.load() == SLOT_NONE) {
            readySlot.store(writeSlot);   // transfiere ownership a Core 1
            writeSlot ^= 1;               // la próxima ventana se escribe en el otro slot
        }
        // Si aún pendiente (caso patológico): se descarta esta ventana y se
        // sigue escribiendo el mismo slot. KISS: sin colas, sin locks, sin
        // bloqueo del audio — un drop de análisis no es audible.
    }
}

void Tuner::processAnalysisCore1() {
    // Core 1: reclama la ventana publicada (o vuelve de inmediato si no hay).
    uint8_t slot = readySlot.exchange(SLOT_NONE);
    if (slot == SLOT_NONE) return;
    performAnalysis(buffers[slot]);
}

void Tuner::performAnalysis(const int16_t* window) {
    // Algoritmo de Diferencia de Magnitud Promedio (AMDF) - Ligero y Efectivo
    uint16_t minPeriod = static_cast<uint16_t>(sampleRate_ / 1000.0f); // 1000Hz max
    uint16_t maxPeriod = static_cast<uint16_t>(sampleRate_ / 70.0f);   // 70Hz min

    uint16_t bestPeriod = 0;
    uint64_t minDiff = 0xFFFFFFFFFFFFFFFF;

    // Solo analizamos si hay volumen suficiente (Puerta de ruido interna)
    int64_t energy = 0;
    for(int i=0; i<BUFFER_SIZE; i++) energy += abs(window[i]);
    if (energy < (BUFFER_SIZE * 100)) {
        signalActive = false;
        return;
    }

    // Buscar el periodo fundamental
    for (uint16_t tau = minPeriod; tau < maxPeriod; tau++) {
        uint64_t diff = 0;
        for (uint16_t i = 0; i < BUFFER_SIZE - tau; i++) {
            diff += abs(window[i] - window[i + tau]);
        }

        if (diff < minDiff) {
            minDiff = diff;
            bestPeriod = tau;
        }
    }

    if (bestPeriod == 0) return;

    float frequency = sampleRate_ / static_cast<float>(bestPeriod);

    // BUG FIX #1: Guard clause para prevenir bucle infinito si frequency es 0.0f o NaN
    if (frequency <= 0.0f || !isfinite(frequency)) return;

    // Normalizar a la octava central (A4)
    while (frequency < 420.0f) frequency *= 2.0f;
    while (frequency > 860.0f) frequency /= 2.0f;

    // Encontrar la nota más cercana
    float minFreqDiff = 1000.0f;
    uint8_t bestNote = 0;

    for (uint8_t i = 0; i < 12; i++) {
        float d = fabsf(frequency - noteFrequencies[i]);
        if (d < minFreqDiff) {
            minFreqDiff = d;
            bestNote = i;
        }
    }

    // Calcular desviación en cents: 1200 * log2(f/f0)
    float cents = 1200.0f * log2f(frequency / noteFrequencies[bestNote]);

    detectedNoteIdx = bestNote;
    centsOffset = static_cast<int8_t>(cents);
    signalActive = true;
}

const char* Tuner::getNoteName() {
    return noteNames[detectedNoteIdx.load()];
}

int8_t Tuner::getCents() {
    return centsOffset.load();
}
