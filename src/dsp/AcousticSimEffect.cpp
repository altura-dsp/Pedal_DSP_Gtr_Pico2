#include "../config/HardwareConfig.h"
#include "AcousticSimEffect.h"
#include <algorithm>
#include <math.h>

AcousticSimEffect::AcousticSimEffect()
    : enabled(false), sampleRate_(globalSampleRate) {
    targetBody.store(0.5f, std::memory_order_relaxed);
    targetBrilliance.store(0.5f, std::memory_order_relaxed);
    reset();
}

void AcousticSimEffect::init(sample_t sampleRate) {
        // FIX-1 (P0 vptr bug): memset eliminado
    sampleRate_ = sampleRate;
    reset();
    recalculateCoefficients();
}

void AcousticSimEffect::reset() {
    for(int i=0; i<4; i++) {
        states[i].state1 = 0.0f;
        states[i].state2 = 0.0f;
    }
}

void AcousticSimEffect::recalculateCoefficients() {
    uint8_t nextIdx = activeCoeffsIdx.load(std::memory_order_relaxed) ^ 1;
    
    sample_t tBody = targetBody.load(std::memory_order_relaxed);
    sample_t tBrilliance = targetBrilliance.load(std::memory_order_relaxed);
    
    sample_t baseFreqs[4] = { 100.0f, 220.0f, 450.0f, 800.0f };
    sample_t compFactor = 1.0f + tBrilliance;
    
    for(int i=0; i<4; i++) {
        sample_t freq = baseFreqs[i] * (1.5f - tBody);
        sample_t q = 5.0f + (tBrilliance * 10.0f);
        
        coeffs[nextIdx][i].f_coeff = 2.0f * sinf((sample_t)M_PI * freq / sampleRate_);
        coeffs[nextIdx][i].damp = 1.0f / q;
        coeffs[nextIdx][i].compFactor = compFactor;
    }
    
    activeCoeffsIdx.store(nextIdx, std::memory_order_release);
}

inline sample_t AcousticSimEffect::processSVF(sample_t in, const AcousticCoeffs& c, AcousticState& s) {
    sample_t notch = in - c.damp * s.state1;
    sample_t low = s.state2 + c.f_coeff * s.state1;
    sample_t high = notch - low;
    sample_t band = c.f_coeff * high + s.state1;
    
    s.state1 = band;
    s.state2 = low;
    
    return band;
}

void __not_in_flash_func(AcousticSimEffect::processBlock)(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
    if (!enabled) return;

    uint8_t activeIdx = activeCoeffsIdx.load(std::memory_order_acquire);
    sample_t compFactor = coeffs[activeIdx][0].compFactor;

    for (size_t i = 0; i < numSamples; ++i) {
        sample_t inL = left[i];
        sample_t inR = right[i];
        sample_t inMono = (inL + inR) * 0.5f;

        sample_t resonances = 0.0f;
        for(int m=0; m<4; m++) {
            resonances += processSVF(inMono, coeffs[activeIdx][m], states[m]);
        }

        // Mezclar con la señal original (Dry + Resonancias)
        sample_t out = inMono + (resonances * 0.2f);
        
        // ZLWarm Inflator Asymmetric Shaper (Armónicos Pares para "Madera")
        out = out * compFactor;
        
        sample_t x = out;
        if (x > 0.0f) {
            // Positive Shaper
            sample_t x2 = x * x;
            out = x - 0.2f * x2 - 0.15f * x2 * x;
        } else {
            // Negative Shaper (Más agresivo para asimetría par)
            sample_t x2 = x * x;
            out = x + 0.3f * x2 - 0.15f * x2 * x;
        }

        // Hard clamp final de seguridad
        if (out > 1.0f) out = 1.0f;
        else if (out < -1.0f) out = -1.0f;

        left[i] = out;
        right[i] = out;
    }
}

// ========== FASE SOLID: Interfaz de parámetros ==========

uint8_t AcousticSimEffect::getParamCount() const { return 2; }

ParamInfo AcousticSimEffect::getParamInfo(uint8_t index) const { 
    switch (index) {
        case 0: return {"Body", 0.0f, 1.0f, "%.2f", 0.05f, ParamCurve::LINEAR};
        case 1: return {"Brilliance", 0.0f, 1.0f, "%.2f", 0.05f, ParamCurve::LINEAR};
        default: return {"", 0.0f, 0.0f, "", 0.0f, ParamCurve::LINEAR};
    }
}

sample_t AcousticSimEffect::getParamValue(uint8_t index) const { 
    switch (index) {
        case 0: return targetBody.load(std::memory_order_relaxed);
        case 1: return targetBrilliance.load(std::memory_order_relaxed);
        default: return 0.0f;
    }
}

void AcousticSimEffect::setParamValue(uint8_t index, sample_t value) { 
    sample_t clamped = std::clamp(value, 0.0f, 1.0f);
    switch (index) {
        case 0: targetBody.store(clamped, std::memory_order_relaxed); break;
        case 1: targetBrilliance.store(clamped, std::memory_order_relaxed); break;
    }
    recalculateCoefficients();
}
