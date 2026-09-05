#pragma once

#include <Arduino.h>
#include "hardware/sync.h"  // Para PROGMEM en RP2350

/**
 * @file MathUtils.h
 * @brief Utilidades matemáticas de alto rendimiento para DSP
 *
 * Todas las funciones están diseñadas para:
 * - Minimizar ciclos de CPU usando instrucciones de hardware
 * - Usar FPU nativa del RP2350
 */

// ============================================
// CONSTANTES DE PUNTO FIJO
// ============================================

namespace DSPConstants {
    // Q23 (23 bits fracción para audio 24-bit)
    constexpr int32_t Q23_MAX  =  8388607;   // 2^23 - 1 (máximo sample positivo)
    constexpr int32_t Q23_MIN  = -8388608;   // -2^23 (mínimo sample negativo)

    // Q15 (15 bits fracción para LoFi)
    constexpr int16_t Q15_MAX  = 32767;
    constexpr int16_t Q15_MIN  = -32768;

    inline int16_t clampQ15(int16_t x) {
        return (x > Q15_MAX) ? Q15_MAX : ((x < Q15_MIN) ? Q15_MIN : x);
    }
}

namespace MathUtils {

    /**
     * @brief Versión FPU flotante nativa para coeficientes de filtro LPF
     * @param normalizedFreq Frecuencia normalizada [0, 1] donde 1 = Nyquist
     * @return Coeficiente alpha en float
     */
    inline float lpf_alpha_float(float normalizedFreq) {
        constexpr float SCALE = 6.28318530718f;  // 2*pi
        return expf(-normalizedFreq * SCALE);
    }

    /**
     * @brief Aproximación extremadamente rápida de exp(x).
     * Ideal para emular curvas no lineales (Shockley, etc.) sin bloquear la FPU con llamadas pesadas.
     * @param x Exponente
     * @return Aproximación de exp(x)
     */
    inline float fast_exp_approx(float x) {
        // Aproximación rápida polinomial base-2
        // e^x = 2^(x / ln(2))
        x *= 1.44269504f;
        float frac = x - (int)x;
        float base = (1.0f + frac * (0.693147f + frac * 0.240226f)); // Taylor aproximado
        return base * (float)(1 << (int)x);
    }

    // --------------------------------------------
    // SUAVIZADO DE PARÁMETROS
    // --------------------------------------------

    /**
     * @brief Interpolación exponencial hacia valor objetivo
     * @param current Valor actual
     * @param target Valor objetivo
     * @param coef Coeficiente [0, 1] (típico: 0.001-0.01)
     * @return Nuevo valor suavizado
     *
     * Fórmula: current += (target - current) * coef
     */
    inline float smooth_param(float current, float target, float coef) {
        return current + (target - current) * coef;
    }
}
