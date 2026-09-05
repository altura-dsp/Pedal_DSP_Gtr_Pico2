#pragma once

/**
 * @brief Suavizado de parámetros para evitar zipper noise
 *
 * Implementa filtro exponencial (low-pass) de un polo.
 * Fórmula: current += (target - current) * coef
 *
 * Uso típico en parámetros de audio:
 * - Ganancia de distorsión
 * - Mezcla wet/dry
 * - Frecuencias de filtro
 * - Threshold de noise gate
 *
 * Tiempo de establecimiento (~99%):
 * - coef 0.001 → ~1000 muestras (~21ms @ 48kHz)
 * - coef 0.01  → ~100 muestras (~2ms @ 48kHz)
 * - coef 0.1   → ~10 muestras (~0.2ms @ 48kHz)
 */
class SmoothedValue {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    /**
     * @brief Constructor
     * @param initial Valor inicial
     * @param coefSmooth Coeficiente de suavizado (0.0-1.0)
     *        Más bajo = más suave pero más lento
     */
    constexpr SmoothedValue(float initial = 0.0f, float coefSmooth = 0.01f)
        : current(initial), target(initial), coef(coefSmooth) {}

    /**
     * @brief Establece nuevo valor objetivo (thread-safe para single writer)
     * @param val Nuevo valor deseado
     */
    void setTarget(float val) { target = val; }

    /**
     * @brief Obtiene valor actual y avanza el filtro una muestra
     * @return Valor suavizado actualizado
     */
    float getNext() {
        current += (target - current) * coef;
        return current;
    }

    /**
     * @brief Obtiene valor actual sin avanzar el filtro
     */
    float getCurrent() const { return current; }

    /**
     * @brief Obtiene valor objetivo sin modificar estado
     */
    float getTarget() const { return target; }

    /**
     * @brief Forza valor inmediato (sin suavizado)
     * Útil para preset change o bypass
     */
    void resetTo(float val) {
        current = val;
        target = val;
    }

    /**
     * @brief Verifica si el valor está cerca del objetivo
     * @param tolerance Tolerancia (default 0.001)
     */
    bool isSettled(float tolerance = 0.001f) const {
        return fabsf(target - current) < tolerance;
    }

    /**
     * @brief Cambia velocidad de suavizado
     * @param newCoef Nuevo coeficiente (0.0-1.0)
     */
    void setCoef(float newCoef) { coef = newCoef; }

    /**
     * @brief Calcula coeficiente para tiempo específico
     * @param timeMs Tiempo de establecimiento en ms
     * @param sampleRate Tasa de muestreo
     * @return Coeficiente aproximado
     *
     * Para filtro exponencial, tiempo de establecimiento ≈ 4/coef
     * Por lo tanto: coef ≈ 4 / (timeMs * sampleRate / 1000)
     */
    static constexpr float calculateCoef(float timeMs, float sampleRate) {
        float samples = (timeMs / 1000.0f) * sampleRate;
        return 4.0f / (samples + 1.0f);
    }

private:
    float current;  ///< Valor actual (salida del filtro)
    float target;   ///< Valor objetivo (entrada deseadA)
    float coef;     ///< Coeficiente de suavizado (0-1)
};
