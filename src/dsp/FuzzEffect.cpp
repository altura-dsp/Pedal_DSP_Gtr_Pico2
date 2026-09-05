#include "../config/HardwareConfig.h"
#include "FuzzEffect.h"
#include "pico.h"
#include "../audio/MathUtils.h"

FuzzEffect::FuzzEffect() : enabled(false), sampleRate_(globalSampleRate) {
    reset();
}

void FuzzEffect::init(sample_t sampleRate) {
        // FIX-1 (P0 vptr bug): memset eliminado
    sampleRate_ = (sampleRate > 0) ? sampleRate : globalSampleRate;
    // DC Blocker cut frequency (~5Hz)
    dcBlockAlpha = MathUtils::lpf_alpha_float(5.0f / sampleRate_);
    reset();                        // re-init limpia dcBlockL/R (antes lo hacia el memset, destruyendo el vptr)
    recalculateCoefficients();
}

void FuzzEffect::reset() {
    dcBlockL = dcBlockR = 0.0f;
}

void FuzzEffect::recalculateCoefficients() {
    uint8_t nextIdx = activeCoeffsIdx.load(std::memory_order_relaxed) ^ 1;
    
    coeffs[nextIdx].gain = 1.0f + targetGain.load(std::memory_order_relaxed) * 49.0f;
    coeffs[nextIdx].bias = -0.5f * targetBias.load(std::memory_order_relaxed);
    coeffs[nextIdx].level = targetLevel.load(std::memory_order_relaxed);
    
    activeCoeffsIdx.store(nextIdx, std::memory_order_release);
}

void __not_in_flash_func(FuzzEffect::processBlock)(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
    if (!enabled.load(std::memory_order_relaxed)) return;

    // 1. Parameter Shadowing
    uint8_t activeIdx = activeCoeffsIdx.load(std::memory_order_acquire);
    sample_t gain = coeffs[activeIdx].gain;
    sample_t bias = coeffs[activeIdx].bias;
    sample_t level = coeffs[activeIdx].level;

    // 2. Procesamiento de Bloque
    for (size_t i = 0; i < numSamples; ++i) {
        // --- Canal Izquierdo ---
        sample_t xL = left[i] * gain;
        xL = fuzzClip(xL, bias);
        
        // DC Blocker (HPF muy bajo) para centrar la señal asimétrica
        dcBlockL += dcBlockAlpha * (xL - dcBlockL);
        xL -= dcBlockL;
        
        left[i] = xL * level;

        // --- Canal Derecho ---
        sample_t xR = right[i] * gain;
        xR = fuzzClip(xR, bias);
        
        dcBlockR += dcBlockAlpha * (xR - dcBlockR);
        xR -= dcBlockR;
        
        right[i] = xR * level;
    }
}

// ========== FASE SOLID: Implementación de interfaz genérica ==========

uint8_t FuzzEffect::getParamCount() const { return 3; }

ParamInfo FuzzEffect::getParamInfo(uint8_t i) const {
    switch(i) {
        case 0: return {"Gain", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        case 1: return {"Bias", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        case 2: return {"Level", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        default: return {"", 0.0f, 0.0f, "", 0.0f, ParamCurve::LINEAR};
    }
}

sample_t FuzzEffect::getParamValue(uint8_t i) const {
    return (i == 0) ? targetGain.load() : (i == 1) ? targetBias.load() : targetLevel.load();
}

void FuzzEffect::setParamValue(uint8_t i, sample_t v) {
    if (i == 0) targetGain.store(v, std::memory_order_relaxed);
    else if (i == 1) targetBias.store(v, std::memory_order_relaxed);
    else targetLevel.store(v, std::memory_order_relaxed);
    recalculateCoefficients();
}
