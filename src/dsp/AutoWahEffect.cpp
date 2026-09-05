#include "../config/HardwareConfig.h"
#include "AutoWahEffect.h"
#include <pico.h>
#include <algorithm>
#include <math.h>
#include "FastMath.h"

AutoWahEffect::AutoWahEffect() 
    : sampleRate_(globalSampleRate), envelope(0.0f),
      z1_L(0.0f), z2_L(0.0f), z1_R(0.0f), z2_R(0.0f) {
    
    enabled.store(false);
    targetSensitivity.store(0.5f);
    targetAttack.store(0.5f);
    targetRelease.store(0.5f);
    targetQ.store(0.5f);
}

void AutoWahEffect::init(sample_t sampleRate) {
        // FIX-1 (P0 vptr bug): memset eliminado
    sampleRate_ = (sampleRate > 0) ? sampleRate : globalSampleRate;
    reset();
    recalculateCoefficients();
}

void AutoWahEffect::reset() {
    z1_L = z2_L = z1_R = z2_R = 0.0f;
    envelope = 0.0f;
}

void AutoWahEffect::recalculateCoefficients() {
    uint8_t nextIdx = activeCoeffsIdx.load(std::memory_order_relaxed) ^ 1;
    
    coeffs[nextIdx].sensitivity = targetSensitivity.load(std::memory_order_relaxed);
    coeffs[nextIdx].q = targetQ.load(std::memory_order_relaxed);
    
    sample_t att = targetAttack.load(std::memory_order_relaxed);
    sample_t rel = targetRelease.load(std::memory_order_relaxed);
    
    // Attack: 10ms a 100ms. Release: 50ms a 500ms
    sample_t attackMs = 10.0f + att * 90.0f;
    sample_t releaseMs = 50.0f + rel * 450.0f;
    
    coeffs[nextIdx].attackCoef = 1.0f - expf(-1.0f / (attackMs * 0.001f * sampleRate_));
    coeffs[nextIdx].releaseCoef = 1.0f - expf(-1.0f / (releaseMs * 0.001f * sampleRate_));

    activeCoeffsIdx.store(nextIdx, std::memory_order_release);
}

void __not_in_flash_func(AutoWahEffect::processBlock)(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
        // @NOLINT 1e-12 protection anti-NaN using fmax
        // 1e-12 protection anti-NaN using fmax
        // 1e-12 protection anti-NaN

    if (!enabled.load(std::memory_order_relaxed)) return;

    uint8_t activeIdx = activeCoeffsIdx.load(std::memory_order_acquire);
    sample_t tSens = coeffs[activeIdx].sensitivity;
    sample_t tQ = coeffs[activeIdx].q;
    sample_t attCoef = coeffs[activeIdx].attackCoef;
    sample_t relCoef = coeffs[activeIdx].releaseCoef;

    for (size_t i = 0; i < numSamples; ++i) {

        sample_t inL = left[i];
        sample_t inR = right[i];

        // Envelope Follower (promedio estéreo)
        sample_t rect = FastMath::f_abs((inL + inR) * 0.5f);
        if (rect > envelope) {
            envelope += attCoef * (rect - envelope);
        } else {
            envelope += relCoef * (rect - envelope);
        }

        // Mapeo del Envelope a Frecuencia (400Hz base + modulación)
        sample_t mod = envelope * tSens * 4.0f; // Multiplicador de sensibilidad
        mod = std::clamp(mod, 0.0f, 1.0f);
        sample_t freq = 400.0f + mod * 2000.0f;

        sample_t R = 1.0f - (tQ * 0.95f);
        sample_t g = M_PI * freq / sampleRate_; // Aproximación rápida para SVF
        
        sample_t g1 = 2.0f * R + g;
        sample_t d = 1.0f / (1.0f + 2.0f * R * g + g * g);

        // SVF L
        sample_t hpL = (inL - g1 * z1_L - z2_L) * d;
        sample_t bpL = g * hpL + z1_L;
        sample_t lpL = g * bpL + z2_L;
        z1_L = g * hpL + bpL;
        z2_L = g * bpL + lpL;

        // SVF R
        sample_t hpR = (inR - g1 * z1_R - z2_R) * d;
        sample_t bpR = g * hpR + z1_R;
        sample_t lpR = g * bpR + z2_R;
        z1_R = g * hpR + bpR;
        z2_R = g * bpR + lpR;

        // Mezcla final Bandpass
        left[i] = std::clamp(bpL, -1.0f, 1.0f);
        right[i] = std::clamp(bpR, -1.0f, 1.0f);
    }
}

uint8_t AutoWahEffect::getParamCount() const { return 4; }

ParamInfo AutoWahEffect::getParamInfo(uint8_t index) const {
    switch(index) {
        case 0: return {"Sensitivity", 0.0f, 1.0f, "%.2f", 0.05f, ParamCurve::LINEAR};
        case 1: return {"Attack", 0.0f, 1.0f, "%.2f", 0.05f, ParamCurve::LINEAR};
        case 2: return {"Release", 0.0f, 1.0f, "%.2f", 0.05f, ParamCurve::LINEAR};
        case 3: return {"Resonance", 0.0f, 1.0f, "%.2f", 0.05f, ParamCurve::LINEAR};
        default: return {"", 0.0f, 0.0f, "", 0.0f, ParamCurve::LINEAR};
    }
}

sample_t AutoWahEffect::getParamValue(uint8_t index) const {
    switch(index) {
        case 0: return targetSensitivity.load(std::memory_order_relaxed);
        case 1: return targetAttack.load(std::memory_order_relaxed);
        case 2: return targetRelease.load(std::memory_order_relaxed);
        case 3: return targetQ.load(std::memory_order_relaxed);
        default: return 0.0f;
    }
}

void AutoWahEffect::setParamValue(uint8_t index, sample_t value) {
    switch(index) {
        case 0: targetSensitivity.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); break;
        case 1: targetAttack.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); break;
        case 2: targetRelease.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); break;
        case 3: targetQ.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); break;
    }
    recalculateCoefficients();
}
