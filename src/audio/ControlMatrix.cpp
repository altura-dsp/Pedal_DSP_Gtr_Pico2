#include "ControlMatrix.h"
#include "Arduino.h"

void ControlMatrix::init() {
    // Inicializar todos los slots
    for (int i = 0; i < MATRIX_SOURCES; i++) {
        slots[i].sourceValue.store(0.0f);
        slots[i].lastUpdate.store(0);
        slots[i].sourceType = SOURCE_NONE;
        slots[i].targetEffect = 0;
        slots[i].targetParam = 0;
        slots[i].enabled = false;
    }

    lastUpdateTime = millis();
}

void ControlMatrix::updateSource(uint8_t slotIdx, float value) {
    if (slotIdx >= MATRIX_SOURCES) return;

    // Actualización atómica del valor
    slots[slotIdx].sourceValue.store(value);

    // Actualizar timestamp para resolución de conflictos
    uint32_t now = millis();
    slots[slotIdx].lastUpdate.store(now);
}

float ControlMatrix::readSource(uint8_t slotIdx) {
    if (slotIdx >= MATRIX_SOURCES) return 0.0f;

    // Lectura atómica del valor (lock-free)
    return slots[slotIdx].sourceValue.load();
}

void ControlMatrix::configureSlot(uint8_t slotIdx, MatrixSource source, uint8_t effect, uint8_t param) {
    if (slotIdx >= MATRIX_SOURCES) return;

    slots[slotIdx].sourceType = source;
    slots[slotIdx].targetEffect = effect;
    slots[slotIdx].targetParam = param;
}

void ControlMatrix::setSlotEnabled(uint8_t slotIdx, bool enabled) {
    if (slotIdx >= MATRIX_SOURCES) return;

    slots[slotIdx].enabled = enabled;
}

void ControlMatrix::resolveConflicts() {
    // Si múltiples fuentes atacan el mismo destino, la más reciente gana
    // Esto se implementa automáticamente por timestamp en updateSource()

    // Opcional: Implementar lógica de prioridad más compleja aquí
    // Por ahora, el timestamp implícito en lastUpdate es suficiente
}

float ControlMatrix::getMappedValue(uint8_t effect, uint8_t param) {
    // Buscar slot activo que coincida con effect/param
    float latestValue = -1.0f;  // -1.0 = no mapeo
    uint32_t latestTimestamp = 0;

    for (int i = 0; i < MATRIX_SOURCES; i++) {
        if (!slots[i].enabled) continue;

        if (slots[i].targetEffect == effect && slots[i].targetParam == param) {
            uint32_t slotTime = slots[i].lastUpdate.load();

            // La fuente más reciente domina
            if (slotTime > latestTimestamp) {
                latestTimestamp = slotTime;
                latestValue = slots[i].sourceValue.load();
            }
        }
    }

    return latestValue;
}

// ============================================================================
// Día 3 - WavePedal (LFO) Implementation
// ============================================================================

void WavePedal::init(ControlMatrix* ctrlMatrix) {
    matrix = ctrlMatrix;
    phase = 0.0f;
    rate = 1.0f;  // 1Hz default
    wave = 0;      // Sine default
    lastUpdate = millis();
}

void WavePedal::process(float sampleRate) {
    if (matrix == nullptr) return;

    uint32_t now = millis();

    // Update rate: 100Hz (10ms)
    if (now - lastUpdate >= 10) {
        // Calcular incremento de fase
        // rate (Hz) * 0.01 (10ms) = incremento por ciclo
        float phaseIncrement = rate * 0.01f;
        phase += phaseIncrement;

        // Wrap phase 0.0-1.0
        if (phase >= 1.0f) phase -= 1.0f;

        // Generar output según forma de onda
        float value = 0.0f;
        switch (wave) {
            case 0: value = sineWave(phase); break;
            case 1: value = triangleWave(phase); break;
            case 2: value = sawtoothWave(phase); break;
            case 3: value = squareWave(phase); break;
            default: value = sineWave(phase); break;
        }

        // Actualizar ControlMatrix slot
        matrix->updateSource(SOURCE_WAVE_PEDAL_LFO, value);

        lastUpdate = now;
    }
}

// ============================================================================
// Día 3 - InputLevel (Envelope Follower) Implementation
// ============================================================================

void InputLevel::init(ControlMatrix* ctrlMatrix) {
    matrix = ctrlMatrix;
    envelope = 0.0f;
    attack = 0.001f;   // 1ms default
    release = 0.1f;    // 100ms default
    lastUpdate = millis();
}

void InputLevel::processSample(float inputSample) {
    // Rectificar señal (valor absoluto)
    float rectified = fabs(inputSample);

    // Limitar a rango 0.0-1.0
    if (rectified > 1.0f) rectified = 1.0f;

    // Detectar pico para attack
    if (rectified > envelope) {
        // Attack: seguimiento rápido de picos
        // Coeficiente aproximado para tiempo de attack dado
        float attackCoeff = 0.1f;  // Ajustar según attack time real
        envelope += (rectified - envelope) * attackCoeff;
    } else {
        // Release: decaimiento lento
        // Coeficiente aproximado para tiempo de release dado
        float releaseCoeff = 0.001f;  // Ajustar según release time real
        envelope += (rectified - envelope) * releaseCoeff;
    }

    // Prevenir valores negativos
    if (envelope < 0.0f) envelope = 0.0f;
}

void InputLevel::updateMatrix() {
    if (matrix == nullptr) return;

    uint32_t now = millis();

    // Update rate: 100Hz (10ms)
    if (now - lastUpdate >= 10) {
        // Normalizar envolvente a rango float 0.0 - 1.0
        float value = envelope;
        if (value > 1.0f) value = 1.0f;

        // Actualizar ControlMatrix slot
        matrix->updateSource(SOURCE_INPUT_LEVEL_ENV, value);

        lastUpdate = now;
    }
}
