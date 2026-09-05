#pragma once

#include <stdint.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923f
#endif

/**
 * @brief FastMath - Algoritmos algebraicos acelerados para DSP
 * 
 * Reemplaza llamadas costosas a la FPU (sinf, cosf, expf) con aproximaciones 
 * de alta velocidad (Bhaskara I, Padé, Hack de Schraudolph) diseñadas para
 * minimizar el uso de ciclos en el Cortex-M33 (RP2350).
 */
namespace FastMath {

    /**
     * @brief Aproximación parabólica (tipo Bhaskara I) de Seno
     * Error máximo absoluto de ~0.001. Extremadamente rápido en Cortex-M.
     * @param x Ángulo en radianes (debe estar pre-empaquetado en [-PI, PI]).
     */
    inline float fast_sinf(float x) __attribute__((always_inline));
    inline float fast_sinf(float x) {
        // Range-Reduction ultra-rápida (int casts) para prevenir cuelgues del FPU con fases extremas
        const float INV_TWO_PI_LOCAL = 0.159154943091895f;
        const float TWO_PI_LOCAL = 6.283185307179586f;
        int32_t k = (int32_t)(x * INV_TWO_PI_LOCAL + (x > 0.0f ? 0.5f : -0.5f));
        x -= (float)k * TWO_PI_LOCAL;

        const float B = 4.0f / M_PI;
        const float C = -4.0f / (M_PI * M_PI);

        float y = B * x + C * x * (x < 0.0f ? -x : x);

        // Suavizado final para precisión extra (P = 0.225)
        const float P = 0.225f;
        return P * (y * (y < 0.0f ? -y : y) - y) + y;
    }

    /**
     * @brief Aproximación parabólica de Coseno
     * Basado en el fast_sinf desplazado por PI/2.
     * @param x Ángulo en radianes.
     */
    inline float fast_cosf(float x) __attribute__((always_inline));
    inline float fast_cosf(float x) {
        x += M_PI_2;
        return fast_sinf(x);
    }

    /**
     * @brief Hack de Nicol Schraudolph para expf rápido usando IEEE-754
     * Manipula directamente los bits del exponente del float.
     * Extremadamente rápido, precisión aceptable para factores de inercia o filtros LPF.
     * @param x Exponente.
     */
    inline float fast_expf(float x) __attribute__((always_inline));
    inline float fast_expf(float x) {
        // Evitar underflow completo en cálculos de audio
        if (x < -80.0f) return 0.0f;
        
        union { float f; int32_t i; } reinterpretor;
        // 12102203.0f = (1 << 23) / ln(2)
        // 1064866805 = 127 * (1 << 23) - corrección
        reinterpretor.i = (int32_t)(12102203.0f * x + 1064866805.0f);
        return reinterpretor.f;
    }

    /**
     * @brief Aproximación rápida para 2^x (Usado en el cálculo de decibelios del EQ)
     */
    inline float fast_exp2f(float x) __attribute__((always_inline));
    inline float fast_exp2f(float x) {
        union { float f; int32_t i; } reinterpretor;
        reinterpretor.i = (int32_t)((1 << 23) * (x + 126.94269504f));
        return reinterpretor.f;
    }

    /**
     * @brief Valor absoluto rápido (resuelto a VABS.F32 en ARM)
     * Evita el overhead o bugs de enlazado de fabsf()
     */
    inline float f_abs(float x) __attribute__((always_inline));
    inline float f_abs(float x) {
        return x < 0.0f ? -x : x;
    }

} // namespace FastMath
