#include "../config/HardwareConfig.h"
#include "MojoEffect.h"
#include "pico.h"
#include <math.h>

MojoEffect::MojoEffect() : enabled(false), sampleRate_(globalSampleRate) {}

void MojoEffect::init(sample_t sampleRate) {
        // FIX-1 (P0 vptr bug): memset eliminado
    sampleRate_ = (sampleRate > 0) ? sampleRate : globalSampleRate;
    recalculateCoefficients();
}

void MojoEffect::reset() {
    // Sin estado
}

void MojoEffect::recalculateCoefficients() {
    uint8_t nextIdx = activeCoeffsIdx.load(std::memory_order_relaxed) ^ 1;
    
    coeffs[nextIdx].mojo = targetMojo.load(std::memory_order_relaxed) * 2.0f;
    coeffs[nextIdx].mix = targetMix.load(std::memory_order_relaxed);
    
    activeCoeffsIdx.store(nextIdx, std::memory_order_release);
}

#ifndef PI
#define PI 3.14159265358979323846f
#endif
#ifndef TWO_PI
#define TWO_PI 6.28318530717958647692f
#endif

// Aproximación Bhaskara I para seno (ultra rápida y limpia para DSP)
inline sample_t sin_bhandaskara_i(sample_t x) {
    if (x < 0.0f) x += TWO_PI;
    if (x > TWO_PI) x -= TWO_PI;

    if (x > PI) {
        x = TWO_PI - x;
    }

    sample_t num = 16.0f * x * (PI - x);
    sample_t den = 5.0f * PI * PI - 4.0f * x * (PI - x);
    return (den != 0.0f) ? (num / den) : 0.0f;  // Semántica ternaria (Regla 6): prevenir NaN
}

void __not_in_flash_func(MojoEffect::processBlock)(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
    if (!enabled.load(std::memory_order_relaxed)) return;

    // 1. Parameter Shadowing
    uint8_t activeIdx = activeCoeffsIdx.load(std::memory_order_acquire);
    sample_t mojoVal = coeffs[activeIdx].mojo;
    sample_t mixVal = coeffs[activeIdx].mix;

    auto processMojo = [&](sample_t sample) {
        sample_t drySample = sample;

        // 2. Pliegue asimétrico suave (Wavefolding core)
        if (sample > 1.0f) {
            sample = 1.0f - (sample - 1.0f) * mojoVal;
        } else if (sample < -1.0f) {
            sample = -1.0f - (sample + 1.0f) * mojoVal;
        }

        // 3. Suavizado asintótico final usando Bhaskara I (~5-8 ciclos)
        sample = sin_bhandaskara_i(sample * 1.57079633f);

        // 4. Mezcla (Dry/Wet)
        sample = (sample * mixVal) + (drySample * (1.0f - mixVal));

        // 5. Safety Clamp final
        return std::clamp(sample, -1.0f, 1.0f);
    };

    for (size_t i = 0; i < numSamples; ++i) {
        left[i] = processMojo(left[i]);
        right[i] = processMojo(right[i]);
    }
}

// ========== FASE SOLID: Implementación de interfaz genérica ==========

uint8_t MojoEffect::getParamCount() const { return 2; }

ParamInfo MojoEffect::getParamInfo(uint8_t index) const {
    switch(index) {
        case 0: return {"Mojo", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        case 1: return {"Mix", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        default: return {"", 0.0f, 0.0f, "", 0.0f, ParamCurve::LINEAR};
    }
}

sample_t MojoEffect::getParamValue(uint8_t index) const {
    switch(index) {
        case 0: return targetMojo.load();
        case 1: return targetMix.load();
        default: return 0.0f;
    }
}

void MojoEffect::setParamValue(uint8_t index, sample_t value) {
    switch(index) {
        case 0: targetMojo.store(value, std::memory_order_relaxed); break;
        case 1: targetMix.store(value, std::memory_order_relaxed); break;
    }
    recalculateCoefficients();
}
