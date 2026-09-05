#include "../config/HardwareConfig.h"
#include "DistortionEffect.h"
#include "../audio/MathUtils.h"
#include "../audio/utils/DspMathFast.h"
#include <math.h>
#include "FastMath.h"
#include <algorithm>

DistortionEffect::DistortionEffect() 
    : sampleRate_(globalSampleRate) {
    
    enabled.store(false);
    mode.store(DistortionMode::CLASSIC_DIODE);
    
    targetGain.store(0.5f);
    targetTone.store(0.5f);
    targetVolume.store(0.5f);
    
    reset();
}

void DistortionEffect::init(sample_t sampleRate) {
        // FIX-1 (P0 vptr bug): memset eliminado
    sampleRate_ = (sampleRate > 0) ? sampleRate : globalSampleRate;
    reset();
    recalculateCoefficients();
}

void DistortionEffect::reset() {
    lpStateL = lpStateR = 0.0f;
    prevXL = prevXR = 0.0f;
    prevDistL = prevDistR = 0.0f;
    dcBlockerL = dcBlockerR = 0.0f;
    prevInL = prevInR = 0.0f;
}

void DistortionEffect::recalculateCoefficients() {
    uint8_t nextIdx = activeCoeffsIdx.load(std::memory_order_relaxed) ^ 1;
    
    coeffs[nextIdx].gain = targetGain.load(std::memory_order_relaxed);
    coeffs[nextIdx].volume = targetVolume.load(std::memory_order_relaxed);
    
    sample_t tTone = targetTone.load(std::memory_order_relaxed);
    sample_t freqHz = 400.0f + (tTone * 7600.0f);
    coeffs[nextIdx].filter_alpha = 1.0f - expf(-2.0f * M_PI * freqHz / sampleRate_);
    
    coeffs[nextIdx].mode = mode.load(std::memory_order_relaxed);

    activeCoeffsIdx.store(nextIdx, std::memory_order_release);
}

inline sample_t DistortionEffect::diodeClip(sample_t x) const {
    // Ecuación asimétrica polinomial en float32
    sample_t x_clamp = std::clamp(x, -1.0f, 1.0f);
    
    sample_t x2 = x_clamp * x_clamp;
    sample_t x3 = x2 * x_clamp;
    
    sample_t k3 = (x_clamp >= 0.0f) ? 0.55f : 0.35f;
    sample_t y = x_clamp - k3 * x3;

    sample_t absX = FastMath::f_abs(x_clamp);
    if (absX > 0.6f) {
        sample_t x5 = x3 * x2;
        sample_t k5 = (x_clamp >= 0.0f) ? 0.18f : 0.10f;
        y += k5 * x5;
    }

    return std::clamp(y, -1.0f, 1.0f);
}

inline sample_t DistortionEffect::softsignClip(sample_t x) const {
    // Fast Langevin/Tube Sigmoid: fastTanh
    // Extremadamente rápido, 10 ciclos vs 50.
    sample_t yf = dspmath::fastTanh(x);
    yf *= 1.25f; // Compensación de ganancia suave original
    return std::clamp(yf, -1.0f, 1.0f);
}

void DistortionEffect::processBlock(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
    if (!enabled.load(std::memory_order_relaxed)) return;

    uint8_t activeIdx = activeCoeffsIdx.load(std::memory_order_acquire);
    sample_t drive = 1.0f + coeffs[activeIdx].gain * 49.0f;
    sample_t vol = coeffs[activeIdx].volume;
    sample_t filter_alpha_val = coeffs[activeIdx].filter_alpha;
    DistortionMode currMode = coeffs[activeIdx].mode;

    for (size_t i = 0; i < numSamples; ++i) {

        // Canal L
        sample_t xL = left[i] * drive;
        sample_t midL = (xL + prevXL) * 0.5f;
        prevXL = xL;
        
        sample_t distMidL, distXL;
        if (currMode == DistortionMode::WAVENET_SOFTSIGN) {
            distMidL = softsignClip(midL);
            distXL = softsignClip(xL);
        } else {
            distMidL = diodeClip(midL);
            distXL = diodeClip(xL);
        }
        
        sample_t xL_down = (prevDistL + (distMidL * 2.0f) + distXL) * 0.25f;
        prevDistL = distXL;
        
        lpStateL += filter_alpha_val * (xL_down - lpStateL);
        sample_t outL = lpStateL * vol;
        
        // DC Blocker (R = 0.999f)
        sample_t hpfL = outL - prevInL + 0.999f * dcBlockerL;
        prevInL = outL;
        dcBlockerL = hpfL;
        
        left[i] = std::clamp(hpfL, -1.0f, 1.0f);

        // Canal R
        sample_t xR = right[i] * drive;
        sample_t midR = (xR + prevXR) * 0.5f;
        prevXR = xR;
        
        sample_t distMidR, distXR;
        if (currMode == DistortionMode::WAVENET_SOFTSIGN) {
            distMidR = softsignClip(midR);
            distXR = softsignClip(xR);
        } else {
            distMidR = diodeClip(midR);
            distXR = diodeClip(xR);
        }
        
        sample_t xR_down = (prevDistR + (distMidR * 2.0f) + distXR) * 0.25f;
        prevDistR = distXR;
        
        lpStateR += filter_alpha_val * (xR_down - lpStateR);
        sample_t outR = lpStateR * vol;
        
        sample_t hpfR = outR - prevInR + 0.999f * dcBlockerR;
        prevInR = outR;
        dcBlockerR = hpfR;
        
        right[i] = std::clamp(hpfR, -1.0f, 1.0f);
    }
}

uint8_t DistortionEffect::getParamCount() const { return 3; }

ParamInfo DistortionEffect::getParamInfo(uint8_t index) const {
    switch(index) {
        case 0: return {"Drive", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        case 1: return {"Tone", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        case 2: return {"Level", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        default: return {"", 0.0f, 0.0f, "", 0.0f, ParamCurve::LINEAR};
    }
}

sample_t DistortionEffect::getParamValue(uint8_t index) const {
    switch(index) {
        case 0: return targetGain.load(std::memory_order_relaxed);
        case 1: return targetTone.load(std::memory_order_relaxed);
        case 2: return targetVolume.load(std::memory_order_relaxed);
        default: return 0.0f;
    }
}

void DistortionEffect::setParamValue(uint8_t index, sample_t value) {
    switch(index) {
        case 0: targetGain.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); break;
        case 1: targetTone.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); break;
        case 2: targetVolume.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); break;
    }
    recalculateCoefficients();
}
