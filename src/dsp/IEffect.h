#pragma once

#include "../config/HardwareConfig.h"
#include <Arduino.h>
#include "DSPTypes.h"

/**
 * @brief Estructura de metadatos para parámetros de efectos (Fase SOLID)
 *
 * Permite que la UI conozca: nombre, rango, formato y delta de cambio
 * sin conocer detalles internos del efecto.
 */
enum class ParamCurve {
    LINEAR,
    LOGARITHMIC,
    EXPONENTIAL
};

struct ParamInfo {
    const char* name;      // Nombre del parámetro ("Time", "Feedback")
    sample_t min;             // Valor mínimo nativo
    sample_t max;             // Valor máximo nativo
    const char* format;    // Formato snprintf ("%.0f ms", "%.1f Hz")
    sample_t stepSize;        // Delta por click de encoder (0.05f, 10.0f, etc.)
    ParamCurve curve;      // Curva de mapeo para el potenciómetro
};

/**
 * @brief Interfaz base para todos los efectos de audio (Fase SOLID)
 *
 * Garantiza que todos los efectos tengan:
 * - Método de procesamiento estándar (process)
 * - Control de activación/bypass (setEnabled)
 * - Acceso genérico a parámetros (elimina acoplamiento UI-DSP)
 *
 * NO SE DEBEN usar asignaciones dinámicas (new/malloc) dentro de estas implementaciones.
 */
class IEffect {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    virtual ~IEffect() = default;

    /**
     * @brief Inicializa el efecto (ej. precalcular coeficientes).
     * @param sampleRate Frecuencia de muestreo (ej. (int)globalSampleRate).
     */
    virtual void init(sample_t sampleRate) = 0;

    /**
     * @brief Procesa un bloque de muestras estéreo en formato sample_t [-1.0f, 1.0f].
     *
     * @param left Puntero al buffer del canal izquierdo.
     * @param right Puntero al buffer del canal derecho.
     * @param numSamples Número de muestras en el bloque.
     */
    virtual void processBlock(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) = 0;

    /**
     * @brief Limpia los buffers internos del efecto (previene pops al cambiar).
     */
    virtual void reset() = 0;

    /**
     * @brief Activa o desactiva el efecto (Bypass).
     */
    virtual void setEnabled(bool state) = 0;

    /**
     * @brief Verifica si el efecto está activado.
     */
    virtual bool isEnabled() const = 0;

    /**
     * @brief Activa o desactiva el modo de solo colas (Spill-over).
     * En este modo, el efecto no recibe entrada nueva pero sigue procesando
     * el audio residual de sus buffers internos.
     */
    virtual void setTailOnly(bool state) { (void)state; } // Default: No hace nada

    /**
     * @return true si el efecto está en modo solo colas.
     */
    virtual bool isTailOnly() const { return false; }

    /**
     * @brief Sincroniza el efecto con un Master BPM global.
     * @param bpm Beats por minuto (ej. 120.0f).
     */
    virtual void setTempoBpm(sample_t bpm) { (void)bpm; }

    // ========== FASE SOLID: Interfaz genérica de parámetros ==========

    /**
     * @brief Retorna el número de parámetros de este efecto.
     * @return Cantidad de parámetros (0-18 según efecto)
     */
    virtual uint8_t getParamCount() const = 0;

    /**
     * @brief Retorna metadatos completos de un parámetro específico.
     * @param index Índice del parámetro (0 a getParamCount()-1)
     * @return Struct ParamInfo con nombre, rango, formato y stepSize
     */
    virtual ParamInfo getParamInfo(uint8_t index) const = 0;

    /**
     * @brief Retorna el valor actual de un parámetro.
     * @param index Índice del parámetro
     * @return Valor actual (en rango nativo del efecto)
     */
    virtual sample_t getParamValue(uint8_t index) const = 0;

    /**
     * @brief Establece el valor de un parámetro.
     * @param index Índice del parámetro
     * @param value Nuevo valor (será clampeado al rango nativo)
     */
    virtual void setParamValue(uint8_t index, sample_t value) = 0;
};
