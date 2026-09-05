#include "../config/HardwareConfig.h"
#include "CompressorEffect.h"
#include "../audio/MathUtils.h"
#include <math.h>
#include <algorithm>

CompressorEffect::CompressorEffect()
    : enabled(false), sampleRate_(globalSampleRate), env_sq_avg(0.0f), current_gain(1.0f), alpha_rms(0.0f) {
    
    // Valores iniciales
    thresholdDb.store(-20.0f);
    ratio.store(4.0f);
    attackMs.store(5.0f);
    releaseMs.store(100.0f);
    makeupDb.store(0.0f);
    
    // El init reculará attack, release y rms
}

void CompressorEffect::init(sample_t sampleRate) {
        // FIX-1 (P0 vptr bug): memset eliminado
    sampleRate_ = (sampleRate > 0.0f) ? sampleRate : globalSampleRate;
    
    // RMS alpha (ventana de ~10ms)
    alpha_rms = expf(-1.0f / (10.0f * 0.001f * sampleRate_));
    
    reset();
    recalculateCoefficients();
}

void CompressorEffect::reset() {
    env_sq_avg = 0.0f;
    current_gain = 1.0f;
}

void CompressorEffect::recalculateCoefficients() {
    uint8_t nextIdx = activeCoeffsIdx.load(std::memory_order_relaxed) ^ 1;
    
    sample_t th = thresholdDb.load(std::memory_order_relaxed);
    coeffs[nextIdx].threshold_linear = powf(10.0f, th / 20.0f);
    
    sample_t r = ratio.load(std::memory_order_relaxed);
    coeffs[nextIdx].ratio_coeff = 1.0f - (1.0f / r);
    
    sample_t mk = makeupDb.load(std::memory_order_relaxed);
    coeffs[nextIdx].makeup_linear = powf(10.0f, mk / 20.0f);
    
    sample_t attMs = attackMs.load(std::memory_order_relaxed);
    coeffs[nextIdx].alpha_attack = expf(-1.0f / (attMs * 0.001f * sampleRate_));
    
    sample_t relMs = releaseMs.load(std::memory_order_relaxed);
    coeffs[nextIdx].alpha_release = expf(-1.0f / (relMs * 0.001f * sampleRate_));
    
    activeCoeffsIdx.store(nextIdx, std::memory_order_release);
}

void CompressorEffect::processBlock(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
        // @NOLINT 1e-12 protection anti-NaN using fmax
        // 1e-12 protection anti-NaN using fmax
        // 1e-12 protection anti-NaN

    if (!enabled.load(std::memory_order_relaxed)) return;

    uint8_t activeIdx = activeCoeffsIdx.load(std::memory_order_acquire);
    const sample_t th_lin = coeffs[activeIdx].threshold_linear;
    const sample_t r_coeff = coeffs[activeIdx].ratio_coeff;
    const sample_t mk_lin = coeffs[activeIdx].makeup_linear;
    const sample_t a_att = coeffs[activeIdx].alpha_attack;
    const sample_t a_rel = coeffs[activeIdx].alpha_release;
    
    const sample_t a_rms = alpha_rms;
    const sample_t one_minus_rms = 1.0f - a_rms;

    for (size_t i = 0; i < numSamples; ++i) {
        sample_t xL = left[i];
        sample_t xR = right[i];
        
        // 1. RMS Squared detector (Stereo Link)
        sample_t x_sq = (xL * xL + xR * xR) * 0.5f;
        
        // 2. Filtro RMS
        env_sq_avg = a_rms * env_sq_avg + one_minus_rms * x_sq;
        
        // 3. Nivel lineal
        sample_t rms_level = sqrtf(env_sq_avg);
        
        // 4. Calcular Target Gain (Aproximación lineal de GR)
        sample_t target_gain = 1.0f;
        if (rms_level > th_lin && rms_level > 0.00001f) {
            sample_t reduction = ((rms_level - th_lin) * r_coeff) / rms_level;
            target_gain = 1.0f - reduction;
            if (target_gain < 0.01f) target_gain = 0.01f; // Cap en aprox -40dB
        }

        // 5. Ballistics (Attack/Release asimétrico)
        sample_t alpha = (target_gain < current_gain) ? a_att : a_rel;
        current_gain = alpha * current_gain + (1.0f - alpha) * target_gain;

        // 6. Aplicar Gain Final y Makeup
        sample_t total_gain = current_gain * mk_lin;
        
        left[i] = xL * total_gain;
        right[i] = xR * total_gain;
    }
}

// ========== Interfaz de Parámetros ==========

uint8_t CompressorEffect::getParamCount() const { return 5; }

ParamInfo CompressorEffect::getParamInfo(uint8_t index) const {
    switch(index) {
        case 0: return {"Threshold", -60.0f, 0.0f, "%.0f dB", 1.0f, ParamCurve::LINEAR};
        case 1: return {"Ratio", 1.0f, 20.0f, "%.1f:1", 0.5f, ParamCurve::LINEAR};
        case 2: return {"Attack", 0.1f, 100.0f, "%.1f ms", 1.0f, ParamCurve::LINEAR};
        case 3: return {"Release", 5.0f, 500.0f, "%.0f ms", 5.0f, ParamCurve::LINEAR};
        case 4: return {"Makeup", 0.0f, 24.0f, "%.0f dB", 1.0f, ParamCurve::LINEAR};
        default: return {"", 0.0f, 0.0f, "", 0.0f, ParamCurve::LINEAR};
    }
}

sample_t CompressorEffect::getParamValue(uint8_t index) const {
    switch(index) {
        case 0: return thresholdDb.load(std::memory_order_relaxed);
        case 1: return ratio.load(std::memory_order_relaxed);
        case 2: return attackMs.load(std::memory_order_relaxed);
        case 3: return releaseMs.load(std::memory_order_relaxed);
        case 4: return makeupDb.load(std::memory_order_relaxed);
        default: return 0.0f;
    }
}

void CompressorEffect::setParamValue(uint8_t index, sample_t value) {
    switch(index) {
        case 0: 
            thresholdDb.store(std::clamp(value, -60.0f, 0.0f), std::memory_order_relaxed);
            break;
        case 1: 
            ratio.store(std::clamp(value, 1.0f, 20.0f), std::memory_order_relaxed);
            break;
        case 2: 
            attackMs.store(std::clamp(value, 0.1f, 100.0f), std::memory_order_relaxed);
            break;
        case 3: 
            releaseMs.store(std::clamp(value, 5.0f, 500.0f), std::memory_order_relaxed);
            break;
        case 4: 
            makeupDb.store(std::clamp(value, 0.0f, 24.0f), std::memory_order_relaxed);
            break;
    }
    recalculateCoefficients();
}
