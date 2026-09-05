#include "../config/HardwareConfig.h"
#include "ParametricEqEffect.h"
#include <pico.h>
#include <algorithm>
#include <math.h>

ParametricEqEffect::ParametricEqEffect() : sampleRate_(globalSampleRate) {
    enabled.store(false);

    // Frecuencias base razonables
    targetFreq[0].store(100.0f);
    targetFreq[1].store(250.0f);
    targetFreq[2].store(800.0f);
    targetFreq[3].store(2000.0f);
    targetFreq[4].store(5000.0f);
    targetFreq[5].store(10000.0f);

    for (int i = 0; i < 6; ++i) {
        targetQ[i].store(1.0f);
        targetGain[i].store(0.0f); // 0 dB
    }
}

void ParametricEqEffect::init(sample_t sampleRate) {
        // FIX-1 (P0 vptr bug): memset eliminado
    sampleRate_ = (sampleRate > 0) ? sampleRate : globalSampleRate;
    reset();
    recalculateCoefficients();
}

void ParametricEqEffect::reset() {
    for (int i = 0; i < 6; ++i) {
        states[i].z1_L = states[i].z2_L = states[i].z1_R = states[i].z2_R = 0.0f;
    }
}

void ParametricEqEffect::recalculateCoefficients() {
    uint8_t nextIdx = activeCoeffsIdx.load(std::memory_order_relaxed) ^ 1;

    for (int i = 0; i < 6; ++i) {
        sample_t f = targetFreq[i].load(std::memory_order_relaxed);
        sample_t q = targetQ[i].load(std::memory_order_relaxed);
        sample_t gain = targetGain[i].load(std::memory_order_relaxed);

        sample_t A = powf(10.0f, gain / 40.0f);
        sample_t w0 = 2.0f * M_PI * f / sampleRate_;
        sample_t sn = sinf(w0);
        sample_t cs = cosf(w0);
        sample_t alpha = sn / (2.0f * q);

        // Aproximación de TPT de forma simplificada usando g y R
        sample_t g = tanf(w0 / 2.0f);
        sample_t R = 1.0f / (2.0f * q);
        
        coeffs[nextIdx][i].K = A; // A como ganancia lin
        coeffs[nextIdx][i].g = g;
        coeffs[nextIdx][i].R = R;
        coeffs[nextIdx][i].g1 = 2.0f * R + g;
        coeffs[nextIdx][i].d = 1.0f / (1.0f + 2.0f * R * g + g * g);
    }
    
    activeCoeffsIdx.store(nextIdx, std::memory_order_release);
}

void __not_in_flash_func(ParametricEqEffect::processBlock)(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
    if (!enabled.load(std::memory_order_relaxed)) return;

    uint8_t activeIdx = activeCoeffsIdx.load(std::memory_order_acquire);

    for (size_t i = 0; i < numSamples; ++i) {
        sample_t inL = left[i];
        sample_t inR = right[i];

        for (int b = 0; b < 6; ++b) {
            sample_t g = coeffs[activeIdx][b].g;
            sample_t d = coeffs[activeIdx][b].d;
            sample_t g1 = coeffs[activeIdx][b].g1;
            sample_t gain_lin = coeffs[activeIdx][b].K; // Ya precalculado como A = powf(...)

            // Left
            sample_t hpL = (inL - g1 * states[b].z1_L - states[b].z2_L) * d;
            sample_t bpL = g * hpL + states[b].z1_L;
            sample_t lpL = g * bpL + states[b].z2_L;
            states[b].z1_L = g * hpL + bpL;
            states[b].z2_L = g * bpL + lpL;

            // Filtro tipo Bell: salida = LP + BP * (A^2 - 1)*Q + HP
            // Para simplificar y ahorrar CPU, haremos un simple boost/cut paralelo:
            sample_t outL = inL + bpL * (gain_lin - 1.0f);
            inL = outL;

            // Right
            sample_t hpR = (inR - g1 * states[b].z1_R - states[b].z2_R) * d;
            sample_t bpR = g * hpR + states[b].z1_R;
            sample_t lpR = g * bpR + states[b].z2_R;
            states[b].z1_R = g * hpR + bpR;
            states[b].z2_R = g * bpR + lpR;

            sample_t outR = inR + bpR * (gain_lin - 1.0f);
            inR = outR;
        }
        
        left[i] = std::clamp(inL, -1.0f, 1.0f);
        right[i] = std::clamp(inR, -1.0f, 1.0f);
    }
}

uint8_t ParametricEqEffect::getParamCount() const { return 18; }

ParamInfo ParametricEqEffect::getParamInfo(uint8_t index) const {
    uint8_t band = index / 3;
    uint8_t type = index % 3;
    
    // Nombres estáticos por convención
    static const char* names[18] = {
        "F1 Freq", "F1 Q", "F1 Gain", "F2 Freq", "F2 Q", "F2 Gain",
        "F3 Freq", "F3 Q", "F3 Gain", "F4 Freq", "F4 Q", "F4 Gain",
        "F5 Freq", "F5 Q", "F5 Gain", "F6 Freq", "F6 Q", "F6 Gain"
    };

    if (type == 0) return {names[index], 20.0f, 16000.0f, "%.0f Hz", 0.05f, ParamCurve::LOGARITHMIC};
    if (type == 1) return {names[index], 0.1f, 10.0f, "%.2f", 0.1f, ParamCurve::LINEAR};
    return {names[index], -15.0f, 15.0f, "%.1f dB", 0.5f, ParamCurve::LINEAR};
}

sample_t ParametricEqEffect::getParamValue(uint8_t index) const {
    uint8_t band = index / 3;
    uint8_t type = index % 3;
    
    if (type == 0) return targetFreq[band].load(std::memory_order_relaxed);
    if (type == 1) return targetQ[band].load(std::memory_order_relaxed);
    return targetGain[band].load(std::memory_order_relaxed);
}

void ParametricEqEffect::setParamValue(uint8_t index, sample_t value) {
    uint8_t band = index / 3;
    uint8_t type = index % 3;

    if (type == 0) targetFreq[band].store(std::clamp(value, 20.0f, 16000.0f), std::memory_order_relaxed);
    else if (type == 1) targetQ[band].store(std::clamp(value, 0.1f, 10.0f), std::memory_order_relaxed);
    else targetGain[band].store(std::clamp(value, -15.0f, 15.0f), std::memory_order_relaxed);
    
    recalculateCoefficients();
}
