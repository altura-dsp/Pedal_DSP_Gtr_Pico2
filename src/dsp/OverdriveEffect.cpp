#include "../config/HardwareConfig.h"
#include "OverdriveEffect.h"
#include "pico.h"
#include "../audio/MathUtils.h"
#include "../audio/utils/DspMathFast.h"

OverdriveEffect::OverdriveEffect() : enabled(false), sampleRate_(globalSampleRate) {
    reset();
}

void OverdriveEffect::init(sample_t sampleRate) {
        // FIX-1 (P0 vptr bug): memset eliminado
    sampleRate_ = (sampleRate > 0) ? sampleRate : globalSampleRate;
    // Frecuencia fija de pre-EQ (Corte de bajos para evitar "Fuzz" barro)
    preHPF_alpha = MathUtils::lpf_alpha_float(800.0f / sampleRate_);
    reset();                        // re-init limpia los 8 estados de filtro (antes lo hacia el memset, destruyendo el vptr)
    recalculateCoefficients();
}

void OverdriveEffect::reset() {
    preHPF_sL = preHPF_sR = 0.0f;
    postLPF_sL = postLPF_sR = 0.0f;
    prevXL = prevXR = 0.0f;
    prevDistL = prevDistR = 0.0f;
}

void OverdriveEffect::recalculateCoefficients() {
    uint8_t nextIdx = activeCoeffsIdx.load(std::memory_order_relaxed) ^ 1;
    
    coeffs[nextIdx].gain = 1.0f + targetGain.load(std::memory_order_relaxed) * 19.0f;
    coeffs[nextIdx].level = targetLevel.load(std::memory_order_relaxed);
    
    sample_t tone = targetTone.load(std::memory_order_relaxed);
    sample_t freqHz = 1000.0f + (tone * 7000.0f);
    coeffs[nextIdx].tone_alpha = MathUtils::lpf_alpha_float(freqHz / sampleRate_);
    
    activeCoeffsIdx.store(nextIdx, std::memory_order_release);
}

void __not_in_flash_func(OverdriveEffect::processBlock)(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
    if (!enabled.load(std::memory_order_relaxed)) return;

    // 1. Parameter Shadowing (Lectura de Coefficients atómicos)
    uint8_t activeIdx = activeCoeffsIdx.load(std::memory_order_acquire);
    sample_t gain = coeffs[activeIdx].gain;
    sample_t tone_alpha = coeffs[activeIdx].tone_alpha;
    sample_t level = coeffs[activeIdx].level;

    // 2. Procesamiento de Bloque
    for (size_t i = 0; i < numSamples; ++i) {
        // --- Canal Izquierdo ---
        sample_t xL = left[i];
        sample_t midL = (xL + prevXL) * 0.5f; // Upsampling 2x (Interpolación Lineal)
        prevXL = xL;

        auto processSampleL = [&](sample_t sample) {
            preHPF_sL += preHPF_alpha * (sample - preHPF_sL);
            sample_t high = sample - preHPF_sL; // HPF
            return dspmath::goldenRatioClip(high * gain, 1.0f);
        };

        sample_t distMidL = processSampleL(midL);
        sample_t distXL = processSampleL(xL);

        // Downsampling con filtro simple [0.25, 0.5, 0.25]
        sample_t outL = (prevDistL + distMidL * 2.0f + distXL) * 0.25f;
        prevDistL = distXL;

        postLPF_sL += tone_alpha * (outL - postLPF_sL);
        left[i] = postLPF_sL * level;

        // --- Canal Derecho ---
        sample_t xR = right[i];
        sample_t midR = (xR + prevXR) * 0.5f;
        prevXR = xR;

        auto processSampleR = [&](sample_t sample) {
            preHPF_sR += preHPF_alpha * (sample - preHPF_sR);
            sample_t high = sample - preHPF_sR;
            return dspmath::goldenRatioClip(high * gain, 1.0f);
        };

        sample_t distMidR = processSampleR(midR);
        sample_t distXR = processSampleR(xR);

        sample_t outR = (prevDistR + distMidR * 2.0f + distXR) * 0.25f;
        prevDistR = distXR;

        postLPF_sR += tone_alpha * (outR - postLPF_sR);
        right[i] = postLPF_sR * level;
    }
}

// ========== FASE SOLID: Implementación de interfaz genérica ==========

uint8_t OverdriveEffect::getParamCount() const { return 3; }

ParamInfo OverdriveEffect::getParamInfo(uint8_t i) const {
    switch(i) {
        case 0: return {"Gain", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        case 1: return {"Tone", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        case 2: return {"Level", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        default: return {"", 0.0f, 0.0f, "", 0.0f, ParamCurve::LINEAR};
    }
}

sample_t OverdriveEffect::getParamValue(uint8_t i) const {
    return (i == 0) ? targetGain.load() : (i == 1) ? targetTone.load() : targetLevel.load();
}

void OverdriveEffect::setParamValue(uint8_t i, sample_t v) {
    if (i == 0) targetGain.store(v, std::memory_order_relaxed);
    else if (i == 1) targetTone.store(v, std::memory_order_relaxed);
    else targetLevel.store(v, std::memory_order_relaxed);
    recalculateCoefficients();
}
