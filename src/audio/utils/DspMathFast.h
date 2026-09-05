#pragma once

#include <cmath>
#include <algorithm>

namespace dspmath {

/**
 * @brief Convierte decibelios a ganancia lineal (des-templatizado a float).
 */
inline float decibelsToGain(float dB, float minusInfinityDb = -100.0f) noexcept
{
    return dB <= minusInfinityDb ? 0.0f : std::pow(10.0f, dB / 20.0f);
}

/**
 * @brief Convierte ganancia lineal a decibelios (des-templatizado a float).
 */
inline float gainToDecibels(float gain, float minusInfinityDb = -100.0f) noexcept
{
    return gain > 0.0f ? std::max(minusInfinityDb, 20.0f * std::log10(gain))
                       : minusInfinityDb;
}

/**
 * @brief Mapea un valor de un rango a otro (interpolación lineal segura).
 */
inline float mapRange(float value, float inMin, float inMax, float outMin, float outMax) noexcept
{
    if (inMin == inMax) return outMin; // Seguridad: previene NaN/Inf
    return outMin + (outMax - outMin) * ((value - inMin) / (inMax - inMin));
}

/**
 * @brief Aproximación ultrarrápida de tanh() usando Padé [5,4].
 * Aproximadamente 5x más rápida que std::tanh, ideal para saturación analógica.
 */
inline float fastTanh(float x) noexcept
{
    // std::clamp previene aliasing evitando el "brickwall" duro.
    x = std::clamp(x, -3.0f, 3.0f);
    
    const float x2 = x * x;
    const float x4 = x2 * x2;
    
    return x * (945.0f + 105.0f * x2 + x4) / (945.0f + 420.0f * x2 + 15.0f * x4);
}

/**
 * @brief Golden Ratio Clipper basado en φ = 1.618. Knee asintótico natural.
 * Transición imperceptible entre señal limpia y distorsionada.
 */
inline float goldenRatioClip(float sample, float ceiling) noexcept
{
    constexpr float PHI = 1.6180339887498948482f;
    float threshold = ceiling / PHI;
    float absSample = std::abs(sample);
    if (absSample <= threshold) return sample;
    float sign = (sample > 0.0f) ? 1.0f : -1.0f;
    float excess = absSample - threshold;
    float range = ceiling - threshold;
    return sign * (threshold + (range * excess) / (excess + range));
}

} // namespace dspmath
