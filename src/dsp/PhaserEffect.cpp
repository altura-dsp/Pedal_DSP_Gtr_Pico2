#include "../config/HardwareConfig.h"
#include "PhaserEffect.h"
#include "../audio/LfoTablesFloat.h"
#include <pico.h>
#include <algorithm>
#include <math.h>
#include "FastMath.h"

PhaserEffect::PhaserEffect() 
    : sampleRate_(globalSampleRate), lfoPhase(0.0f), feedbackL(0.0f), feedbackR(0.0f) {
    
    enabled.store(false);
    targetRate.store(1.0f);
    targetDepth.store(0.8f);
    targetFeedback.store(0.5f);
    targetMix.store(0.5f);

    for (int i = 0; i < 4; i++) {
        stateL[i] = 0.0f;
        stateR[i] = 0.0f;
    }
}

void PhaserEffect::init(sample_t sampleRate) {
        // FIX-1 (P0 vptr bug): memset eliminado
    sampleRate_ = (sampleRate > 0) ? sampleRate : globalSampleRate;
    reset();
    recalculateCoefficients();
}

void PhaserEffect::reset() {
    lfoPhase = 0.0f;
    feedbackL = 0.0f;
    feedbackR = 0.0f;
    for (int i = 0; i < 4; i++) {
        stateL[i] = 0.0f;
        stateR[i] = 0.0f;
    }
}

void PhaserEffect::recalculateCoefficients() {
    uint8_t nextIdx = activeCoeffsIdx.load(std::memory_order_relaxed) ^ 1;
    
    coeffs[nextIdx].rate = targetRate.load(std::memory_order_relaxed);
    coeffs[nextIdx].depth = targetDepth.load(std::memory_order_relaxed);
    coeffs[nextIdx].feedback = targetFeedback.load(std::memory_order_relaxed);
    
    sample_t tMix = targetMix.load(std::memory_order_relaxed);
    sample_t angle = tMix * M_PI_2; 
    coeffs[nextIdx].wetGain = sinf(angle);
    coeffs[nextIdx].dryGain = cosf(angle);
    
    activeCoeffsIdx.store(nextIdx, std::memory_order_release);
}

void __not_in_flash_func(PhaserEffect::processBlock)(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
        // @NOLINT 1e-12 protection anti-NaN using fmax
        // 1e-12 protection anti-NaN using fmax
        // 1e-12 protection anti-NaN

    if (!enabled.load(std::memory_order_relaxed)) return;

    uint8_t activeIdx = activeCoeffsIdx.load(std::memory_order_acquire);
    sample_t tRate = coeffs[activeIdx].rate;
    sample_t tDepth = coeffs[activeIdx].depth;
    sample_t tFeedback = coeffs[activeIdx].feedback;
    sample_t dry = coeffs[activeIdx].dryGain;
    sample_t wet = coeffs[activeIdx].wetGain;

    for (size_t i = 0; i < numSamples; ++i) {

        lfoPhase += tRate / sampleRate_;
        if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;

        sample_t lfoVal = lfo_sin(lfoPhase); 
        
        sample_t c = lfoVal * tDepth * 0.8f;

        sample_t inL = left[i];
        sample_t inR = right[i];

        sample_t stageL = inL + (feedbackL * tFeedback);
        sample_t stageR = inR + (feedbackR * tFeedback);

        for (int j = 0; j < 4; j++) {
            stageL = apf(stageL, stateL[j], c);
            stageR = apf(stageR, stateR[j], c);
        }

        feedbackL = stageL / (1.0f + FastMath::f_abs(stageL));
        feedbackR = stageR / (1.0f + FastMath::f_abs(stageR));

        left[i] = (inL * dry) + (stageL * wet);
        right[i] = (inR * dry) + (stageR * wet);
    }
}

uint8_t PhaserEffect::getParamCount() const { return 4; }

ParamInfo PhaserEffect::getParamInfo(uint8_t index) const {
    switch(index) {
        case 0: return {"Rate", 0.1f, 10.0f, "%.2f Hz", 0.1f, ParamCurve::LINEAR};
        case 1: return {"Depth", 0.0f, 1.0f, "%.2f", 0.05f, ParamCurve::LINEAR};
        case 2: return {"Feedback", -0.95f, 0.95f, "%.2f", 0.05f, ParamCurve::LINEAR};
        case 3: return {"Mix", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        default: return {"", 0.0f, 0.0f, "", 0.0f, ParamCurve::LINEAR};
    }
}

sample_t PhaserEffect::getParamValue(uint8_t index) const {
    switch(index) {
        case 0: return targetRate.load(std::memory_order_relaxed);
        case 1: return targetDepth.load(std::memory_order_relaxed);
        case 2: return targetFeedback.load(std::memory_order_relaxed);
        case 3: return targetMix.load(std::memory_order_relaxed);
        default: return 0.0f;
    }
}

void PhaserEffect::setParamValue(uint8_t index, sample_t value) {
    switch(index) {
        case 0: targetRate.store(std::clamp(value, 0.1f, 10.0f), std::memory_order_relaxed); break;
        case 1: targetDepth.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); break;
        case 2: targetFeedback.store(std::clamp(value, -0.95f, 0.95f), std::memory_order_relaxed); break;
        case 3: targetMix.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); break;
    }
    recalculateCoefficients();
}
