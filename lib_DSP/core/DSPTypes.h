#ifndef DSP_TYPES_H
#define DSP_TYPES_H

/**
 * @file DSPTypes.h
 * @brief SSOT canónico de tipos DSP compartidos — lib_DSP/core (Regla 3 / A16).
 *
 * Punto Único de Verdad de los primitivos de tipo DSP. Se CONSUME vía
 * lib_extra_dirs desde 1_Pedal / 2_Aguilar / 3_Autotune. Prohibido duplicar
 * copias locales en src/ que lo sombreen (anti-patrón A16 — Deriva SSOT, Regla 3).
 *
 * - Regla 3 (Abstracción de Audio): Uso estricto de sample_t
 * - Regla 9 (FTZ y Denormals): ANTI_DENORMAL_CONSTANT en el hot-path
 */

#ifdef __cplusplus
extern "C" {
#endif

// Abstracción para portabilidad (Pico 1 = Fixed-point, Pico 2 = float)
typedef float sample_t;

// Escudo matemático contra colapsos de FPU (Denormal Flush-to-Zero bypass)
// Inyectar esto en acumuladores de IIR y realimentaciones de Delays
#define ANTI_DENORMAL_CONSTANT 1e-22f

// Macro estricta para protección contra divisiones por cero y NaN
#define SAFE_DIVIDE(num, den) ((den) != 0.0f ? ((num) / (den)) : 0.0f)

#ifdef __cplusplus
}
#endif

#endif // DSP_TYPES_H
