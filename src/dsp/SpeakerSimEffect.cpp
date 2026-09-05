#include "../config/HardwareConfig.h"
#include "SpeakerSimEffect.h"
#include "pico.h"
#include "../audio/MathUtils.h"
#include <math.h>
#include <string.h>
#include <algorithm>

SpeakerSimEffect::SpeakerSimEffect() : enabled(false), sampleRate_(globalSampleRate) {
    reset();
}

void SpeakerSimEffect::init(sample_t sampleRate) {
    sampleRate_ = (sampleRate > 0) ? sampleRate : globalSampleRate;
    
    auto initSvf = [&](sample_t freq, sample_t Q, sample_t& g, sample_t& R, sample_t& h) {
        g = tanf(M_PI * freq / sampleRate_);
        R = 1.0f / (2.0f * Q);
        h = 1.0f / (1.0f + 2.0f * R * g + g * g);
    };

    // BPF 1: Body (120Hz, Q 1.5)
    initSvf(120.0f, 1.5f, bpf1_g, bpf1_R, bpf1_h_coef);
    
    // BPF 2: Mid Scoop (600Hz, Q 1.2)
    initSvf(600.0f, 1.2f, bpf2_g, bpf2_R, bpf2_h_coef);
    
    // BPF 3: Presence (2500Hz, Q 2.0)
    initSvf(2500.0f, 2.0f, bpf3_g, bpf3_R, bpf3_h_coef);
    
    // LPF4 Estático
    lpf4_a = MathUtils::lpf_alpha_float(5000.0f / sampleRate_);
    
    recalculateCoefficients();
}

void SpeakerSimEffect::reset() {
    memset(&stateL, 0, sizeof(stateL));
    memset(&stateR, 0, sizeof(stateR));
}

void SpeakerSimEffect::recalculateCoefficients() {
    uint8_t nextIdx = activeCoeffsIdx.load(std::memory_order_relaxed) ^ 1;
    
    sample_t lowCutFreq = targetLowCut.load(std::memory_order_relaxed);
    coeffs[nextIdx].hpf_alpha = MathUtils::lpf_alpha_float(lowCutFreq / sampleRate_);

    sample_t airFreq = targetAir.load(std::memory_order_relaxed);
    coeffs[nextIdx].air_alpha = MathUtils::lpf_alpha_float(airFreq / sampleRate_);

    sample_t bodyDb = targetBody.load(std::memory_order_relaxed);
    sample_t scoopDb = targetMidScoop.load(std::memory_order_relaxed);
    
    coeffs[nextIdx].body_linear = powf(10.0f, bodyDb / 20.0f);
    coeffs[nextIdx].scoop_linear = powf(10.0f, scoopDb / 20.0f);

    activeCoeffsIdx.store(nextIdx, std::memory_order_release);
}

void __not_in_flash_func(SpeakerSimEffect::processBlock)(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
    if (!enabled.load(std::memory_order_relaxed)) return;

    // 1. Parameter Shadowing & Cálculos pesados (1 vez por bloque)
    uint8_t activeIdx = activeCoeffsIdx.load(std::memory_order_acquire);
    sample_t hpf_alpha = coeffs[activeIdx].hpf_alpha;
    sample_t lpf5_alpha = coeffs[activeIdx].air_alpha;
    sample_t bodyGain = coeffs[activeIdx].body_linear;
    sample_t scoopGain = coeffs[activeIdx].scoop_linear;

    sample_t presDb = 6.0f; // Fijo (Boutique preset)
    sample_t presGain = powf(10.0f, presDb / 20.0f);

    sample_t makeUpGain = 1.25f; // ~2dB makeup gain

    auto processChannel = [&](sample_t x, ChannelState& st) {
        // 1. HPF (Box low cut)
        sample_t y = applyHPF(x, st.hpf, hpf_alpha) * 0.5f;

        // 2. Grupo en paralelo (SVF TPT BPFs)
        sample_t p1 = applyTptSvfBpf(x, st.svf1, bpf1_g, bpf1_R, bpf1_h_coef) * bodyGain;
        sample_t p2 = applyTptSvfBpf(x, st.svf2, bpf2_g, bpf2_R, bpf2_h_coef) * scoopGain;
        sample_t p3 = applyTptSvfBpf(x, st.svf3, bpf3_g, bpf3_R, bpf3_h_coef) * presGain;

        y += (p1 + p2 + p3) * 0.5f; // Mezclar (comp ganancia SVF)

        // 3. Etapas de Aire (LPF)
        y = applyLPF(y, st.lpf4, lpf4_a);
        y = applyLPF(y, st.lpf5, lpf5_alpha);

        // 4. Ganancia final y seguridad
        y *= makeUpGain;
        return std::clamp(y, -1.0f, 1.0f);
    };

    // 2. Procesamiento de Bloque FPU Puro
    for (size_t i = 0; i < numSamples; ++i) {
        left[i] = processChannel(left[i], stateL);
        right[i] = processChannel(right[i], stateR);
    }
}

// ========== FASE SOLID: Implementación de interfaz genérica ==========

uint8_t SpeakerSimEffect::getParamCount() const { return 4; }

ParamInfo SpeakerSimEffect::getParamInfo(uint8_t index) const {
    switch(index) {
        case 0: return {"LowCut", 30.0f, 200.0f, "%.0f Hz", 5.0f, ParamCurve::LINEAR};
        case 1: return {"Body", -6.0f, 12.0f, "%.0f dB", 1.0f, ParamCurve::LINEAR};
        case 2: return {"MidScoop", -14.0f, 0.0f, "%.0f dB", 1.0f, ParamCurve::LINEAR};
        case 3: return {"Air", 3000.0f, 10000.0f, "%.0f Hz", 500.0f, ParamCurve::LINEAR};
        default: return {"", 0.0f, 0.0f, "", 0.0f, ParamCurve::LINEAR};
    }
}

sample_t SpeakerSimEffect::getParamValue(uint8_t index) const {
    switch(index) {
        case 0: return targetLowCut.load();
        case 1: return targetBody.load();
        case 2: return targetMidScoop.load();
        case 3: return targetAir.load();
        default: return 0.0f;
    }
}

void SpeakerSimEffect::setParamValue(uint8_t index, sample_t value) {
    switch(index) {
        case 0: targetLowCut.store(value, std::memory_order_relaxed); break;
        case 1: targetBody.store(value, std::memory_order_relaxed); break;
        case 2: targetMidScoop.store(value, std::memory_order_relaxed); break;
        case 3: targetAir.store(value, std::memory_order_relaxed); break;
    }
    recalculateCoefficients();
}
