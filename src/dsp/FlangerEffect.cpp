#include "../config/HardwareConfig.h"
#include "FlangerEffect.h"
#include "../audio/LfoTablesFloat.h"
#include <pico.h>
#include <algorithm>
#include <math.h>
#include "FastMath.h"

FlangerEffect::FlangerEffect() 
    : sampleRate_(globalSampleRate), lfoPhase(0.0f), writeIndex(0), thiranZ1(0.0f) {
    
    enabled.store(false);
    targetRate.store(0.5f);
    targetDepth.store(0.8f);
    targetFeedback.store(0.6f);
    targetMix.store(0.5f);

    for (int i = 0; i < FLANGER_BUFFER_SIZE; i++) {
        buffer[i] = 0.0f;
    }
}

void FlangerEffect::init(sample_t sampleRate) {
        // FIX-1 (P0 vptr bug): memset eliminado
    sampleRate_ = (sampleRate > 0) ? sampleRate : globalSampleRate;
    reset();
    recalculateCoefficients();
}

void FlangerEffect::reset() {
    lfoPhase = 0.0f;
    writeIndex = 0;
    thiranZ1 = 0.0f;
    for (int i = 0; i < FLANGER_BUFFER_SIZE; i++) {
        buffer[i] = 0.0f;
    }
    for (int i = 0; i < FLANGER_BUFFER_SIZE; i++) {
        buffer[i] = 0.0f;
    }
}

void FlangerEffect::recalculateCoefficients() {
    uint8_t nextIdx = activeCoeffsIdx.load(std::memory_order_relaxed) ^ 1;
    
    coeffs[nextIdx].rate = targetRate.load(std::memory_order_relaxed);
    coeffs[nextIdx].depth = targetDepth.load(std::memory_order_relaxed);
    coeffs[nextIdx].feedback = targetFeedback.load(std::memory_order_relaxed);
    coeffs[nextIdx].mix = targetMix.load(std::memory_order_relaxed);
    
    activeCoeffsIdx.store(nextIdx, std::memory_order_release);
}

void __not_in_flash_func(FlangerEffect::processBlock)(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
        // @NOLINT 1e-12 protection anti-NaN using fmax
        // 1e-12 protection anti-NaN using fmax
        // 1e-12 protection anti-NaN

    if (!enabled.load(std::memory_order_relaxed)) return;

    uint8_t activeIdx = activeCoeffsIdx.load(std::memory_order_acquire);
    sample_t tRate = coeffs[activeIdx].rate;
    sample_t tDepth = coeffs[activeIdx].depth;
    sample_t tFeedback = coeffs[activeIdx].feedback;
    sample_t tMix = coeffs[activeIdx].mix;

    for (size_t i = 0; i < numSamples; ++i) {

        lfoPhase += tRate / sampleRate_;
        if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;

        sample_t lfoVal = lfo_sin(lfoPhase); 
        
        sample_t modulatedTimeMs = 4.0f + (lfoVal * tDepth * 3.9f);
        
        sample_t delaySamples = (modulatedTimeMs * 0.001f) * sampleRate_;
        delaySamples = std::clamp(delaySamples, 1.0f, (sample_t)(FLANGER_BUFFER_SIZE - 2));

        uint32_t delayInt = static_cast<uint32_t>(delaySamples);
        sample_t delayFrac = delaySamples - (sample_t)delayInt;

        // FIX (P2): Operador Módulo eliminado. Uso de máscara de bits rápida.
        uint32_t readIdx = (FLANGER_BUFFER_SIZE + writeIndex - delayInt) & (FLANGER_BUFFER_SIZE - 1);
        uint32_t readIdxNext = (readIdx + 1) & (FLANGER_BUFFER_SIZE - 1);

        sample_t inL = left[i];
        sample_t inR = right[i];
        
        sample_t inMono = (inL + inR) * 0.5f;

        sample_t v0 = buffer[readIdx];
        sample_t v1 = buffer[readIdxNext];
        sample_t linearOut = v0 + delayFrac * (v1 - v0);
        
        sample_t thiranCoef = (1.0f - delayFrac) / (1.0f + delayFrac);
        sample_t thiranOut = thiranCoef * (linearOut - thiranZ1) + v0;
        thiranZ1 = linearOut;

        sample_t fb = thiranOut * tFeedback;
        sample_t writeVal = inMono + fb;
        writeVal = writeVal / (1.0f + FastMath::f_abs(writeVal));

        buffer[writeIndex] = writeVal;
        // Avance circular con máscara de bits en lugar de módulo (P2)
        writeIndex = (writeIndex + 1) & (FLANGER_BUFFER_SIZE - 1);

        left[i] = inL + (thiranOut * tMix);
        right[i] = inR + (thiranOut * tMix);
    }
}

uint8_t FlangerEffect::getParamCount() const { return 4; }

ParamInfo FlangerEffect::getParamInfo(uint8_t index) const {
    switch(index) {
        case 0: return {"Rate", 0.1f, 10.0f, "%.2f Hz", 0.1f, ParamCurve::LINEAR};
        case 1: return {"Depth", 0.0f, 1.0f, "%.2f", 0.05f, ParamCurve::LINEAR};
        case 2: return {"Feedback", -0.95f, 0.95f, "%.2f", 0.05f, ParamCurve::LINEAR};
        case 3: return {"Mix", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        default: return {"", 0.0f, 0.0f, "", 0.0f, ParamCurve::LINEAR};
    }
}

sample_t FlangerEffect::getParamValue(uint8_t index) const {
    switch(index) {
        case 0: return targetRate.load(std::memory_order_relaxed);
        case 1: return targetDepth.load(std::memory_order_relaxed);
        case 2: return targetFeedback.load(std::memory_order_relaxed);
        case 3: return targetMix.load(std::memory_order_relaxed);
        default: return 0.0f;
    }
}

void FlangerEffect::setParamValue(uint8_t index, sample_t value) {
    switch(index) {
        case 0: targetRate.store(std::clamp(value, 0.1f, 10.0f), std::memory_order_relaxed); break;
        case 1: targetDepth.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); break;
        case 2: targetFeedback.store(std::clamp(value, -0.95f, 0.95f), std::memory_order_relaxed); break;
        case 3: targetMix.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); break;
    }
    recalculateCoefficients();
}
