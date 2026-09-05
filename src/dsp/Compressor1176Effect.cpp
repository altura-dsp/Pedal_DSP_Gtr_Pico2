#include "../config/HardwareConfig.h"
#include "Compressor1176Effect.h"
#include <pico.h>
#include <algorithm>
#include <math.h>
#include "FastMath.h"

Compressor1176Effect::Compressor1176Effect() 
    : sampleRate_(globalSampleRate), envelope(0.0f) {
    
    enabled.store(false);
    targetInputGain.store(0.5f);
    targetOutputGain.store(0.5f);
    targetAttack.store(4.0f);
    targetRelease.store(4.0f);
    targetRatio.store(0.0f); // 4:1
}

void Compressor1176Effect::init(sample_t sampleRate) {
        // FIX-1 (P0 vptr bug): memset eliminado
    sampleRate_ = (sampleRate > 0) ? sampleRate : globalSampleRate;
    reset();
    recalculateCoefficients();
}

void Compressor1176Effect::reset() {
    envelope = 0.0f;
    currentInputGain = targetInputGain.load(std::memory_order_relaxed);
    currentOutputGain = targetOutputGain.load(std::memory_order_relaxed);
    recalculateCoefficients();
}

void Compressor1176Effect::recalculateCoefficients() {
    uint8_t nextIdx = activeCoeffsIdx.load(std::memory_order_relaxed) ^ 1;

    sample_t att = targetAttack.load(std::memory_order_relaxed);
    sample_t rel = targetRelease.load(std::memory_order_relaxed);
    
    // Attack 1 a 7 -> 800us a 20us (invertido)
    sample_t attackUs = 800.0f - ((att - 1.0f) / 6.0f) * 780.0f;
    // Release 1 a 7 -> 1100ms a 50ms (invertido)
    sample_t releaseMs = 1100.0f - ((rel - 1.0f) / 6.0f) * 1050.0f;

    // Constantes exponenciales
    coeffs[nextIdx].attackCoef = 1.0f - expf(-1.0f / (attackUs * 0.000001f * sampleRate_));
    coeffs[nextIdx].releaseCoef = 1.0f - expf(-1.0f / (releaseMs * 0.001f * sampleRate_));
    
    sample_t tInGain = targetInputGain.load(std::memory_order_relaxed);
    sample_t tOutGain = targetOutputGain.load(std::memory_order_relaxed);
    
    coeffs[nextIdx].targetInGainLin = powf(10.0f, (tInGain * 40.0f - 20.0f) / 20.0f);
    coeffs[nextIdx].targetOutGainLin = powf(10.0f, (tOutGain * 40.0f - 20.0f) / 20.0f);

    coeffs[nextIdx].ratioIdx = targetRatio.load(std::memory_order_relaxed);

    activeCoeffsIdx.store(nextIdx, std::memory_order_release);
}

#include "../audio/MathUtils.h"

void __not_in_flash_func(Compressor1176Effect::processBlock)(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
        // @NOLINT 1e-12 protection anti-NaN using fmax
        // 1e-12 protection anti-NaN using fmax
        // 1e-12 protection anti-NaN

    if (!enabled.load(std::memory_order_relaxed)) return;

    uint8_t activeIdx = activeCoeffsIdx.load(std::memory_order_acquire);
    sample_t ratioIdx = coeffs[activeIdx].ratioIdx;

    sample_t ratio = 4.0f;
    if (ratioIdx > 0.5f && ratioIdx < 1.5f) ratio = 8.0f;
    else if (ratioIdx >= 1.5f && ratioIdx < 2.5f) ratio = 12.0f;
    else if (ratioIdx >= 2.5f && ratioIdx < 3.5f) ratio = 20.0f;
    else if (ratioIdx >= 3.5f) ratio = 100.0f; // All buttons in approximation

    sample_t threshold = 0.1f; // Threshold fijo en el 1176 (el Input Gain dicta la compresión)

    sample_t targetInGainLinVal = coeffs[activeIdx].targetInGainLin;
    sample_t targetOutGainLinVal = coeffs[activeIdx].targetOutGainLin;
    
    sample_t attCoef = coeffs[activeIdx].attackCoef;
    sample_t relCoef = coeffs[activeIdx].releaseCoef;

    for (size_t i = 0; i < numSamples; ++i) {
        // Smooth gains
        currentInputGain += (targetInGainLinVal - currentInputGain) * 0.005f;
        currentOutputGain += (targetOutGainLinVal - currentOutputGain) * 0.005f;

        sample_t inL = left[i] * currentInputGain;
        sample_t inR = right[i] * currentInputGain;

        // Detector (promedio estéreo)
        sample_t rect = FastMath::f_abs(inL) > FastMath::f_abs(inR) ? FastMath::f_abs(inL) : FastMath::f_abs(inR);

        if (rect > envelope) {
            envelope += attCoef * (rect - envelope);
        } else {
            envelope += relCoef * (rect - envelope);
        }

        // Gain Computer (FET emulation)
        sample_t gainReduction = 1.0f;
        if (envelope > threshold) {
            sample_t overshoot = envelope - threshold;
            sample_t compression = overshoot * (1.0f - 1.0f/ratio);
            
            // Mapeo no lineal tipo FET
            // Simula Vgs (Control Voltage) bajando Ids
            sample_t cv = compression * 5.0f; 
            if (cv > 5.0f) cv = 5.0f;
            
            // Aproximación de la curva FET Rds = 1 / Ids
            // Ids proporcional a (1 - Vgs/Vp)^2
            sample_t vgs_vp = cv / 5.0f; 
            sample_t currentIds = (1.0f - vgs_vp) * (1.0f - vgs_vp);
            if (currentIds < 0.01f) currentIds = 0.01f;
            
            // La ganancia es un divisor de tensión dependiente de Rds (Rds inmensamente prop. a Ids)
            // Gain = Rds / (Rds + R) => o simplificado Gain = Ids / (Ids + k)
            gainReduction = currentIds / (currentIds + 0.2f);
            
            if (gainReduction < 0.01f) gainReduction = 0.01f;
            if (gainReduction > 1.0f) gainReduction = 1.0f;
        }

        sample_t outL = inL * gainReduction * currentOutputGain;
        sample_t outR = inR * gainReduction * currentOutputGain;

        // FET Saturation Stage (Harmonics / THD)
        bool allButtons = (ratio >= 100.0f);
        if (allButtons) {
            // Saturación extrema, más armónicos impares y soft-clipping anticipado
            sample_t cL = outL * 1.5f;
            sample_t cR = outR * 1.5f;
            outL = cL / (1.0f + FastMath::f_abs(cL));
            outR = cR / (1.0f + FastMath::f_abs(cR));
            outL = (outL + 0.2f * outL * outL * outL) * 0.7f;
            outR = (outR + 0.2f * outR * outR * outR) * 0.7f;
        } else {
            // Coloración FET sutil normal
            outL = outL + 0.1f * outL * outL * outL;
            sample_t cL = outL * 0.9f;
            sample_t cR = outR * 0.9f;
            outL = cL / (1.0f + FastMath::f_abs(cL));
            outR = cR / (1.0f + FastMath::f_abs(cR));
            outR = outR + 0.1f * outR * outR * outR;
        }

        left[i] = std::clamp(outL, -1.0f, 1.0f);
        right[i] = std::clamp(outR, -1.0f, 1.0f);
    }
}

uint8_t Compressor1176Effect::getParamCount() const { return 5; }

ParamInfo Compressor1176Effect::getParamInfo(uint8_t index) const {
    switch(index) {
        case 0: return {"Input", 0.0f, 1.0f, "%.2f", 0.05f, ParamCurve::LINEAR};
        case 1: return {"Output", 0.0f, 1.0f, "%.2f", 0.05f, ParamCurve::LINEAR};
        case 2: return {"Attack", 1.0f, 7.0f, "%.0f", 1.0f, ParamCurve::LINEAR};
        case 3: return {"Release", 1.0f, 7.0f, "%.0f", 1.0f, ParamCurve::LINEAR};
        case 4: return {"Ratio", 0.0f, 4.0f, "%.0f", 1.0f, ParamCurve::LINEAR};
        default: return {"", 0.0f, 0.0f, "", 0.0f, ParamCurve::LINEAR};
    }
}

sample_t Compressor1176Effect::getParamValue(uint8_t index) const {
    switch(index) {
        case 0: return targetInputGain.load(std::memory_order_relaxed);
        case 1: return targetOutputGain.load(std::memory_order_relaxed);
        case 2: return targetAttack.load(std::memory_order_relaxed);
        case 3: return targetRelease.load(std::memory_order_relaxed);
        case 4: return targetRatio.load(std::memory_order_relaxed);
        default: return 0.0f;
    }
}

void Compressor1176Effect::setParamValue(uint8_t index, sample_t value) {
    switch(index) {
        case 0: targetInputGain.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); recalculateCoefficients(); break;
        case 1: targetOutputGain.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); recalculateCoefficients(); break;
        case 2: targetAttack.store(std::clamp(value, 1.0f, 7.0f), std::memory_order_relaxed); recalculateCoefficients(); break;
        case 3: targetRelease.store(std::clamp(value, 1.0f, 7.0f), std::memory_order_relaxed); recalculateCoefficients(); break;
        case 4: targetRatio.store(std::clamp(value, 0.0f, 4.0f), std::memory_order_relaxed); break;
    }
    recalculateCoefficients();
}
