#include "../config/HardwareConfig.h"
#include "WahEffect.h"
#include <pico.h>
#include <algorithm>
#include <math.h>

WahEffect::WahEffect() 
    : sampleRate_(globalSampleRate),
      z1_L(0.0f), z2_L(0.0f), z1_R(0.0f), z2_R(0.0f) {
    
    enabled.store(false);
    targetPosition.store(0.5f);
    targetQ.store(0.5f);
}

void WahEffect::init(sample_t sampleRate) {
        // FIX-1 (P0 vptr bug): memset eliminado
    sampleRate_ = (sampleRate > 0) ? sampleRate : globalSampleRate;
    reset();
    recalculateCoefficients();      // antes coeffs[2] quedaba sin calcular hasta el 1er setParamValue (el memset lo enmascaraba con ceros)
}

void WahEffect::reset() {
    z1_L = z2_L = z1_R = z2_R = 0.0f;
}

void WahEffect::recalculateCoefficients() {
    uint8_t nextIdx = activeCoeffsIdx.load(std::memory_order_relaxed) ^ 1;
    
    sample_t tPos = targetPosition.load(std::memory_order_relaxed);
    sample_t tQ = targetQ.load(std::memory_order_relaxed);

    sample_t freq = 400.0f + tPos * 1800.0f;
    sample_t R = 1.0f - (tQ * 0.95f); 
    sample_t g = M_PI * freq / sampleRate_;
    
    coeffs[nextIdx].g = g;
    coeffs[nextIdx].g1 = 2.0f * R + g;
    coeffs[nextIdx].d = 1.0f / (1.0f + 2.0f * R * g + g * g);

    activeCoeffsIdx.store(nextIdx, std::memory_order_release);
}

void __not_in_flash_func(WahEffect::processBlock)(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
    if (!enabled.load(std::memory_order_relaxed)) return;

    uint8_t activeIdx = activeCoeffsIdx.load(std::memory_order_acquire);
    sample_t g = coeffs[activeIdx].g;
    sample_t g1 = coeffs[activeIdx].g1;
    sample_t d = coeffs[activeIdx].d;

    for (size_t i = 0; i < numSamples; ++i) {

        // Procesamiento Canal Izquierdo
        sample_t inL = left[i];
        sample_t hpL = (inL - g1 * z1_L - z2_L) * d;
        sample_t bpL = g * hpL + z1_L;
        sample_t lpL = g * bpL + z2_L;
        z1_L = g * hpL + bpL;
        z2_L = g * bpL + lpL;

        // Procesamiento Canal Derecho
        sample_t inR = right[i];
        sample_t hpR = (inR - g1 * z1_R - z2_R) * d;
        sample_t bpR = g * hpR + z1_R;
        sample_t lpR = g * bpR + z2_R;
        z1_R = g * hpR + bpR;
        z2_R = g * bpR + lpR;

        // Salida BandPass (el efecto Wah clásico es mayormente BandPass)
        // Se mezcla un poco del Dry para cuerpo si se desea, pero el estándar es BP.
        // Clampeamos al rango [-1.0, 1.0] por precaución boutique
        left[i] = std::clamp(bpL, -1.0f, 1.0f);
        right[i] = std::clamp(bpR, -1.0f, 1.0f);
    }
}

uint8_t WahEffect::getParamCount() const { return 2; }

ParamInfo WahEffect::getParamInfo(uint8_t index) const {
    switch(index) {
        case 0: return {"Position", 0.0f, 1.0f, "%.2f", 0.05f, ParamCurve::LINEAR};
        case 1: return {"Resonance", 0.0f, 1.0f, "%.2f", 0.05f, ParamCurve::LINEAR};
        default: return {"", 0.0f, 0.0f, "", 0.0f, ParamCurve::LINEAR};
    }
}

sample_t WahEffect::getParamValue(uint8_t index) const {
    switch(index) {
        case 0: return targetPosition.load(std::memory_order_relaxed);
        case 1: return targetQ.load(std::memory_order_relaxed);
        default: return 0.0f;
    }
}

void WahEffect::setParamValue(uint8_t index, sample_t value) {
    switch(index) {
        case 0: targetPosition.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); break;
        case 1: targetQ.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); break;
    }
    recalculateCoefficients();
}
