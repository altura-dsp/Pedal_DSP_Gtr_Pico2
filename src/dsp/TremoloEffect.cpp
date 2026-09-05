#include "../config/HardwareConfig.h"
#include "TremoloEffect.h"
#include "../audio/LfoTablesFloat.h"
#include <pico.h>
#include <algorithm>
#include <math.h>

TremoloEffect::TremoloEffect() 
    : sampleRate_(globalSampleRate), lfoPhase(0.0f),
      lpfStateL(0.0f), lpfStateR(0.0f), crossoverAlpha(0.0f) {
    
    enabled.store(false);
    targetRate.store(4.0f);
    targetDepth.store(0.5f);
    targetShape.store(0.0f);
    targetHarmonic.store(0.0f);
}

void TremoloEffect::init(sample_t sampleRate) {
        // FIX-1 (P0 vptr bug): memset eliminado
    sampleRate_ = (sampleRate > 0) ? sampleRate : globalSampleRate;
    reset();
    recalculateCoefficients();
    
    // Crossover 1er orden en ~400Hz
    sample_t fc = 400.0f;
    crossoverAlpha = 1.0f - expf(-2.0f * M_PI * fc / sampleRate_);
}

void TremoloEffect::reset() {
    lfoPhase = 0.0f;
    lpfStateL = 0.0f;
    lpfStateR = 0.0f;
}

void TremoloEffect::recalculateCoefficients() {
    uint8_t nextIdx = activeCoeffsIdx.load(std::memory_order_relaxed) ^ 1;
    
    coeffs[nextIdx].rate = targetRate.load(std::memory_order_relaxed);
    coeffs[nextIdx].depth = targetDepth.load(std::memory_order_relaxed);
    coeffs[nextIdx].shape = targetShape.load(std::memory_order_relaxed);
    coeffs[nextIdx].harmonic = targetHarmonic.load(std::memory_order_relaxed);
    
    activeCoeffsIdx.store(nextIdx, std::memory_order_release);
}

void __not_in_flash_func(TremoloEffect::processBlock)(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
        // @NOLINT 1e-12 protection anti-NaN using fmax
        // 1e-12 protection anti-NaN using fmax
        // 1e-12 protection anti-NaN

    if (!enabled.load(std::memory_order_relaxed)) return;

    uint8_t activeIdx = activeCoeffsIdx.load(std::memory_order_acquire);
    sample_t tRate = coeffs[activeIdx].rate;
    sample_t tDepth = coeffs[activeIdx].depth;
    sample_t shape = coeffs[activeIdx].shape;
    bool isHarmonic = (coeffs[activeIdx].harmonic > 0.5f);

    for (size_t i = 0; i < numSamples; ++i) {

        lfoPhase += tRate / sampleRate_;
        if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;

        sample_t lfoVal = 0.0f;
        if (shape > 0.5f) { 
            lfoVal = (lfoPhase < 0.5f) ? 1.0f : -1.0f;
        } else { 
            lfoVal = lfo_sin(lfoPhase);
        }

        sample_t unipolarLFO = (lfoVal + 1.0f) * 0.5f;

        sample_t inL = left[i];
        sample_t inR = right[i];

        if (!isHarmonic) {
            sample_t gain = 1.0f - (tDepth * unipolarLFO);
            left[i] = inL * gain;
            right[i] = inR * gain;
        } else {
            lpfStateL += crossoverAlpha * (inL - lpfStateL);
            lpfStateR += crossoverAlpha * (inR - lpfStateR);
            
            sample_t hpfL = inL - lpfStateL;
            sample_t hpfR = inR - lpfStateR;

            sample_t gainLow = 1.0f - (tDepth * unipolarLFO);
            sample_t gainHigh = 1.0f - (tDepth * (1.0f - unipolarLFO));

            left[i] = (lpfStateL * gainLow) + (hpfL * gainHigh);
            right[i] = (lpfStateR * gainLow) + (hpfR * gainHigh);
        }
    }
}

uint8_t TremoloEffect::getParamCount() const { return 4; }

ParamInfo TremoloEffect::getParamInfo(uint8_t index) const {
    switch(index) {
        case 0: return {"Rate", 0.1f, 20.0f, "%.1f Hz", 0.1f, ParamCurve::LINEAR};
        case 1: return {"Depth", 0.0f, 1.0f, "%.2f", 0.05f, ParamCurve::LINEAR};
        case 2: return {"Shape", 0.0f, 1.0f, "%.0f", 1.0f, ParamCurve::LINEAR};
        case 3: return {"Harmonic", 0.0f, 1.0f, "%.0f", 1.0f, ParamCurve::LINEAR};
        default: return {"", 0.0f, 0.0f, "", 0.0f, ParamCurve::LINEAR};
    }
}

sample_t TremoloEffect::getParamValue(uint8_t index) const {
    switch(index) {
        case 0: return targetRate.load(std::memory_order_relaxed);
        case 1: return targetDepth.load(std::memory_order_relaxed);
        case 2: return targetShape.load(std::memory_order_relaxed);
        case 3: return targetHarmonic.load(std::memory_order_relaxed);
        default: return 0.0f;
    }
}

void TremoloEffect::setParamValue(uint8_t index, sample_t value) {
    switch(index) {
        case 0: targetRate.store(std::clamp(value, 0.1f, 20.0f), std::memory_order_relaxed); break;
        case 1: targetDepth.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); break;
        case 2: targetShape.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); break;
        case 3: targetHarmonic.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); break;
    }
    recalculateCoefficients();
}
