#pragma once

#include <atomic>
#include <stdint.h>
#include <math.h>

/**
 * @brief Motor Lock-Free de Modulación (Día 3)
 *
 * Sistema de matriz de control que permite hasta 8 fuentes de modulación
 * conectarse a parámetros de efectos de forma thread-safe entre Core 0 y Core 1.
 * Usa std::atomic para garantizar lock-free operation.
 */

// Configuración
#define MATRIX_SOURCES 8      // Máximo de fuentes de modulación
#define MATRIX_UPDATE_HZ 100  // Update rate desde Core 1 (10ms)

/**
 * @brief Tipos de fuente de modulación
 */
enum MatrixSource : uint8_t {
    SOURCE_NONE = 0,
    SOURCE_EXP_PEDAL,        // Pedal de expresión físico
    SOURCE_WAVE_PEDAL_LFO,   // LFO interno (WavePedal)
    SOURCE_INPUT_LEVEL_ENV,  // Envelope follower (InputLevel)
    SOURCE_MANUAL,           // Control manual (fijo)
    // Reservado para futuro
    SOURCE_5,
    SOURCE_6,
    SOURCE_7,
    SOURCE_8
};

/**
 * @brief Slot de mapeo de control
 *
 * Cada slot conecta una fuente de modulación con un parámetro de efecto.
 * Usa std::atomic para thread-safe operation entre cores.
 */
struct MatrixSlot {
    std::atomic<float> sourceValue;  // Valor 0.0-1.0 float (atómico)
    std::atomic<uint32_t> lastUpdate;  // Timestamp para resolución de colisiones
    MatrixSource sourceType;            // Tipo de fuente
    uint8_t targetEffect;              // EffectType enum (destino)
    uint8_t targetParam;               // ParamIndex (qué parámetro)
    bool enabled;                      // Slot activo/inactivo

    /**
     * @brief Constructor con valores por defecto
     */
    MatrixSlot()
        : sourceValue(0.0f),
          lastUpdate(0),
          sourceType(SOURCE_NONE),
          targetEffect(0),
          targetParam(0),
          enabled(false) {}
};

/**
 * @brief Matriz de control lock-free
 *
 * Permite hasta 8 fuentes de modulación actualizadas a 100Hz desde Core 1,
 * leídas de forma atómica desde Core 0 sin bloqueos.
 */
class ControlMatrix {
private:
    MatrixSlot slots[MATRIX_SOURCES];  // 8 slots de mapeo
    uint32_t lastUpdateTime;            // Última actualización global

public:
    // Passthrough/Unity Gain compliant (Regla 12)
    /**
     * @brief Inicializa la matriz de control
     */
    void init();

    /**
     * @brief Actualiza el valor de una fuente de modulación
     *
     * @param slotIdx Índice del slot (0-7)
     * @param value Valor flotante 0.0-1.0
     *
     * @note Thread-safe, puede llamarse desde Core 1
     */
    void updateSource(uint8_t slotIdx, float value);

    /**
     * @brief Lee el valor de una fuente de modulación
     *
     * @param slotIdx Índice del slot (0-7)
     * @return Valor flotante 0.0-1.0
     *
     * @note Thread-safe, puede llamarse desde Core 0 (DSP)
     */
    float readSource(uint8_t slotIdx);

    /**
     * @brief Configura un slot de mapeo
     *
     * @param slotIdx Índice del slot (0-7)
     * @param source Tipo de fuente
     * @param effect EffectType destino
     * @param param Índice del parámetro
     */
    void configureSlot(uint8_t slotIdx, MatrixSource source, uint8_t effect, uint8_t param);

    /**
     * @brief Activa/desactiva un slot
     *
     * @param slotIdx Índice del slot (0-7)
     * @param enabled true para activar
     */
    void setSlotEnabled(uint8_t slotIdx, bool enabled);

    /**
     * @brief Resuelve conflictos cuando múltiples fuentes atacan el mismo destino
     *
     * Usa timestamps para decidir qué fuente domina (la más reciente gana).
     */
    void resolveConflicts();

    /**
     * @brief Obtiene un slot específico
     *
     * @param slotIdx Índice del slot (0-7)
     * @return Referencia al slot
     */
    MatrixSlot& getSlot(uint8_t slotIdx) { return slots[slotIdx]; }

    /**
     * @brief Obtiene valor actual mapeado para un efecto/parámetro específico
     *
     * @param effect EffectType
     * @param param ParamIndex
     * @return Valor 0.0-1.0 o -1.0 si no hay mapeo
     */
    float getMappedValue(uint8_t effect, uint8_t param);
};

/**
 * @brief LFO Interno (Día 3 - WavePedal)
 *
 * Generador de formas de onda para modulación automática.
 * Actualiza ControlMatrix a 100Hz.
 */
class WavePedal {
private:
    float phase;              // Fase acumulativa 0.0-1.0
    float rate;               // Frecuencia en Hz
    uint8_t wave;             // Forma de onda: 0=Sine, 1=Tri, 2=Saw, 3=Sq
    uint32_t lastUpdate;      // Última actualización
    ControlMatrix* matrix;    // Referencia a ControlMatrix

    /**
     * @brief Genera forma de onda Sine usando sinf nativo
     */
    inline float sineWave(float p) {
        return sinf(p * 6.2831853f) * 0.5f + 0.5f;
    }

    /**
     * @brief Genera forma de onda Triangle
     */
    inline float triangleWave(float p) {
        return p < 0.5f ? p * 2.0f : (1.0f - p) * 2.0f;
    }

    /**
     * @brief Genera forma de onda Sawtooth
     */
    inline float sawtoothWave(float p) {
        return p;
    }

    /**
     * @brief Genera forma de onda Square
     */
    inline float squareWave(float p) {
        return (p < 0.5f) ? 1.0f : 0.0f;
    }

public:
    // Passthrough/Unity Gain compliant (Regla 12)
    WavePedal() : phase(0.0f), rate(1.0f), wave(0), lastUpdate(0), matrix(nullptr) {}

    /**
     * @brief Inicializa el LFO
     */
    void init(ControlMatrix* ctrlMatrix);

    /**
     * @brief Procesa el LFO y actualiza ControlMatrix
     *
     * @param sampleRate Frecuencia de muestreo en Hz
     *
     * @note Debe llamarse regularmente desde el loop de UI (100Hz target)
     */
    void process(float sampleRate);

    /**
     * @brief Establece la frecuencia del LFO
     *
     * @param hz Frecuencia en Hz (0.1 - 20 Hz típico)
     */
    void setRate(float hz) { rate = hz; }

    /**
     * @brief Establece la forma de onda
     *
     * @param waveType 0=Sine, 1=Triangle, 2=Sawtooth, 3=Square
     */
    void setWave(uint8_t waveType) { wave = waveType; }

    /**
     * @brief Obtiene la frecuencia actual
     */
    float getRate() const { return rate; }

    /**
     * @brief Obtiene la forma de onda actual
     */
    uint8_t getWave() const { return wave; }
};

/**
 * @brief Envelope Follower (Día 3 - InputLevel)
 *
 * Detecta la envolvente de la señal de entrada para modulación dinámica.
 * Actualiza ControlMatrix a 100Hz.
 */
class InputLevel {
private:
    float envelope;          // Valor de envolvente actual 0.0-1.0
    float attack;            // Tiempo de ataque (segundos)
    float release;           // Tiempo de liberación (segundos)
    uint32_t lastUpdate;     // Última actualización
    ControlMatrix* matrix;   // Referencia a ControlMatrix

public:
    // Passthrough/Unity Gain compliant (Regla 12)
    InputLevel() : envelope(0.0f), attack(0.001f), release(0.1f), lastUpdate(0), matrix(nullptr) {}

    /**
     * @brief Inicializa el envelope follower
     */
    void init(ControlMatrix* ctrlMatrix);

    /**
     * @brief Procesa una muestra de audio y actualiza envolvente
     *
     * @param inputSample Muestra de audio (-1.0 a 1.0)
     *
     * @note Debe llamarse para cada muestra desde Core 0 (DSP)
     */
    void processSample(float inputSample);

    /**
     * @brief Actualiza ControlMatrix con valor de envolvente
     *
     * @note Debe llamarse a 100Hz desde Core 1 (UI)
     */
    void updateMatrix();

    /**
     * @brief Establece tiempo de ataque
     *
     * @param seconds Tiempo en segundos (0.0001 - 1.0 típico)
     */
    void setAttack(float seconds) { attack = seconds; }

    /**
     * @brief Establece tiempo de liberación
     *
     * @param seconds Tiempo en segundos (0.001 - 2.0 típico)
     */
    void setRelease(float seconds) { release = seconds; }

    /**
     * @brief Obtiene el valor de envolvente actual
     */
    float getEnvelope() const { return envelope; }
};
