#include "../config/HardwareConfig.h"
#include "NoiseGateEffect.h"
#include <math.h>
#include <algorithm>

NoiseGateEffect::NoiseGateEffect() 
    : enabled(false), sampleRate_(globalSampleRate), env_sq_avg(0.0f), current_gain(1.0f), gate_open(true),
      alpha_rms(0.0f), alpha_release(0.0f) {
    
    thresholdDb.store(-40.0f);
    // El init() llamará a recalculateCoefficients
}

void NoiseGateEffect::init(sample_t sampleRate) {
        // FIX-1 (P0 vptr bug): memset eliminado
    sampleRate_ = (sampleRate > 0) ? sampleRate : globalSampleRate;
    
    // RMS alpha (ventana de ~5ms)
    alpha_rms = expf(-1.0f / (0.005f * sampleRate_));
    // Release alpha (atenuación suave ~50ms)
    alpha_release = expf(-1.0f / (0.050f * sampleRate_));
    
    reset();
    recalculateCoefficients();
}

void NoiseGateEffect::reset() {
    env_sq_avg = 0.0f;
    current_gain = 1.0f;
    gate_open = true;
}

void NoiseGateEffect::recalculateCoefficients() {
    uint8_t nextIdx = activeCoeffsIdx.load(std::memory_order_relaxed) ^ 1;
    
    sample_t thDb = thresholdDb.load(std::memory_order_relaxed);
    
    // Lineal open
    coeffs[nextIdx].threshold_open_linear = powf(10.0f, thDb / 20.0f);
    
    // Hysteresis: Umbral de cierre es 4dB más bajo
    coeffs[nextIdx].threshold_close_linear = powf(10.0f, (thDb - 4.0f) / 20.0f);

    activeCoeffsIdx.store(nextIdx, std::memory_order_release);
}

void NoiseGateEffect::processBlock(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
    if (!enabled.load(std::memory_order_relaxed)) return;

    // Parameter Shadowing
    uint8_t activeIdx = activeCoeffsIdx.load(std::memory_order_acquire);
    const sample_t th_open = coeffs[activeIdx].threshold_open_linear;
    const sample_t th_close = coeffs[activeIdx].threshold_close_linear;
    
    const sample_t a_rms = alpha_rms;
    const sample_t one_minus_rms = 1.0f - a_rms;
    const sample_t a_rel = alpha_release;

    for (size_t i = 0; i < numSamples; ++i) {
        sample_t xL = left[i];
        sample_t xR = right[i];
        
        // 1. RMS Squared detector
        sample_t x_sq = (xL * xL + xR * xR) * 0.5f;
        
        // 2. Filtro RMS
        env_sq_avg = a_rms * env_sq_avg + one_minus_rms * x_sq;
        
        // 3. Nivel lineal aproximado
        sample_t rms_level = sqrtf(env_sq_avg);
        
        // 4. Lógica de Hysteresis
        if (gate_open) {
            if (rms_level < th_close) gate_open = false; // Cierra la puerta
        } else {
            if (rms_level > th_open) gate_open = true;   // Abre la puerta
        }
        
        // 5. Suavizado de Ganancia
        if (gate_open) {
            current_gain = 1.0f; // Attack instantáneo
        } else {
            current_gain = a_rel * current_gain; // Release exponencial suave
        }
        
        // 6. Aplicar ganancia a la señal
        if (current_gain < 0.999f) {
            left[i] = xL * current_gain;
            right[i] = xR * current_gain;
        }
    }
}

// ========== FASE SOLID: Implementación de interfaz genérica ==========

uint8_t NoiseGateEffect::getParamCount() const {
    return 1;
}

ParamInfo NoiseGateEffect::getParamInfo(uint8_t index) const {
    switch (index) {
        case 0: return {"Threshold", -60.0f, -10.0f, "%.0f dB", 1.0f, ParamCurve::LINEAR};
        default: return {"", 0.0f, 0.0f, "", 0.0f, ParamCurve::LINEAR};
    }
}

sample_t NoiseGateEffect::getParamValue(uint8_t index) const {
    return thresholdDb.load(std::memory_order_relaxed);
}

void NoiseGateEffect::setParamValue(uint8_t index, sample_t value) {
    if (index == 0) {
        thresholdDb.store(std::clamp(value, -60.0f, -10.0f), std::memory_order_relaxed);
        recalculateCoefficients();
    }
}
