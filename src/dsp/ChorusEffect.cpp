#include "../config/HardwareConfig.h"
#include "ChorusEffect.h"
#include "../audio/LfoTablesFloat.h"
#include <pico.h>
#include <algorithm>
#include <math.h>
#include "FastMath.h"

static sample_t chorusPoolL[MAX_CHORUS_INSTANCES][CHORUS_BUFFER_SIZE];
static sample_t chorusPoolR[MAX_CHORUS_INSTANCES][CHORUS_BUFFER_SIZE];
static std::atomic<bool> chorusPoolInUse[MAX_CHORUS_INSTANCES] = {false, false};

ChorusEffect::ChorusEffect() 
    : sampleRate_(globalSampleRate), lfoPhase(0.0f), writeIndex(0), 
      thiranL1(0.0f), thiranL2(0.0f), thiranR2(0.0f), thiranR3(0.0f),
      poolInstanceId(-1), bufferL(nullptr), bufferR(nullptr) {
    
    enabled.store(false);
    targetRate.store(0.5f);
    targetDepth.store(0.5f);
    targetMix.store(0.5f);
    targetVolume.store(1.0f);

    for (int i = 0; i < MAX_CHORUS_INSTANCES; ++i) {
        bool expected = false;
        if (chorusPoolInUse[i].compare_exchange_strong(expected, true)) {
            poolInstanceId = i;
            bufferL = chorusPoolL[i];
            bufferR = chorusPoolR[i];
            for (int j = 0; j < CHORUS_BUFFER_SIZE; j++) {
                bufferL[j] = 0.0f;
                bufferR[j] = 0.0f;
            }
            break;
        }
    }
}

ChorusEffect::~ChorusEffect() {
    if (poolInstanceId >= 0 && poolInstanceId < MAX_CHORUS_INSTANCES) {
        chorusPoolInUse[poolInstanceId].store(false);
    }
}

void ChorusEffect::init(sample_t sampleRate) {
        // FIX-1 (P0 vptr bug): memset eliminado
    sampleRate_ = (sampleRate > 0) ? sampleRate : globalSampleRate;
    reset();
    recalculateCoefficients();
}

void ChorusEffect::reset() {
    lfoPhase = 0.0f;
    writeIndex = 0;
    thiranL1 = 0.0f; thiranL2 = 0.0f; thiranR2 = 0.0f; thiranR3 = 0.0f;
    bbd_lpf_L = 0.0f; bbd_lpf_R = 0.0f;
    
    if (bufferL) {
        for (int i = 0; i < CHORUS_BUFFER_SIZE; i++) {
            bufferL[i] = 0.0f;
            bufferR[i] = 0.0f;
        }
    }
}

void ChorusEffect::recalculateCoefficients() {
    uint8_t nextIdx = activeCoeffsIdx.load(std::memory_order_relaxed) ^ 1;
    
    coeffs[nextIdx].rate = targetRate.load(std::memory_order_relaxed);
    coeffs[nextIdx].depth = targetDepth.load(std::memory_order_relaxed);
    coeffs[nextIdx].mix = targetMix.load(std::memory_order_relaxed);
    coeffs[nextIdx].volume = targetVolume.load(std::memory_order_relaxed);
    
    activeCoeffsIdx.store(nextIdx, std::memory_order_release);
}

void __not_in_flash_func(ChorusEffect::processBlock)(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
        // @NOLINT 1e-12 protection anti-NaN using fmax
        // 1e-12 protection anti-NaN using fmax
        // 1e-12 protection anti-NaN

    if (!enabled.load(std::memory_order_relaxed) || !bufferL) return;

    uint8_t activeIdx = activeCoeffsIdx.load(std::memory_order_acquire);
    sample_t tRate = coeffs[activeIdx].rate;
    sample_t tDepth = coeffs[activeIdx].depth;
    sample_t tMix = coeffs[activeIdx].mix;
    sample_t tVol = coeffs[activeIdx].volume;

    for (size_t i = 0; i < numSamples; ++i) {
        // Analog drift (micro-ruido TPDF para humanizar el LFO) usando XOR-shift
        // FIX (P2): Operador módulo eliminado en generación de ruido.
        sample_t drift = (sample_t)((xorshift32() & 1023) - 512) * 0.000002f;
        
        lfoPhase += (tRate / sampleRate_) + drift;
        if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
        else if (lfoPhase < 0.0f) lfoPhase += 1.0f;

        sample_t phase1 = lfoPhase;
        sample_t phase2 = lfoPhase + 0.33333f; if (phase2 >= 1.0f) phase2 -= 1.0f;
        sample_t phase3 = lfoPhase + 0.66666f; if (phase3 >= 1.0f) phase3 -= 1.0f;

        sample_t phase2_R = phase2 + 0.25f; if (phase2_R >= 1.0f) phase2_R -= 1.0f;
        sample_t phase3_R = phase3 + 0.25f; if (phase3_R >= 1.0f) phase3_R -= 1.0f;

        sample_t lfo1 = lfo_sin(phase1); 
        sample_t lfo2_L = lfo_sin(phase2);
        sample_t lfo2_R = lfo_sin(phase2_R);
        sample_t lfo3_R = lfo_sin(phase3_R);

        sample_t ms1 = 5.0f + (lfo1 * tDepth * 4.0f);
        sample_t ms2_L = 5.0f + (lfo2_L * tDepth * 4.0f);
        sample_t ms2_R = 5.0f + (lfo2_R * tDepth * 4.0f);
        sample_t ms3_R = 5.0f + (lfo3_R * tDepth * 4.0f);

        sample_t s1 = std::clamp((ms1 * 0.001f) * sampleRate_, 1.0f, (sample_t)(CHORUS_BUFFER_SIZE - 2));
        sample_t s2_L = std::clamp((ms2_L * 0.001f) * sampleRate_, 1.0f, (sample_t)(CHORUS_BUFFER_SIZE - 2));
        sample_t s2_R = std::clamp((ms2_R * 0.001f) * sampleRate_, 1.0f, (sample_t)(CHORUS_BUFFER_SIZE - 2));
        sample_t s3_R = std::clamp((ms3_R * 0.001f) * sampleRate_, 1.0f, (sample_t)(CHORUS_BUFFER_SIZE - 2));

        // BBD Pre-Filter & Compander (Drive)
        sample_t inL = left[i];
        sample_t inR = right[i];
        
        // Clipping suave con Pirkle soft-clipper: x / (1.0f + FastMath::f_abs(x))
        sample_t clippedL = inL * 1.2f;
        sample_t clippedR = inR * 1.2f;
        bufferL[writeIndex] = clippedL / (1.0f + FastMath::f_abs(clippedL));
        bufferR[writeIndex] = clippedR / (1.0f + FastMath::f_abs(clippedR));

        sample_t dL1 = readDelay(bufferL, s1, thiranL1);
        sample_t dL2 = readDelay(bufferL, s2_L, thiranL2);
        sample_t dR2 = readDelay(bufferR, s2_R, thiranR2);
        sample_t dR3 = readDelay(bufferR, s3_R, thiranR3);

        // FIX (P2): Operador Módulo eliminado.
        writeIndex = (writeIndex + 1) & (CHORUS_BUFFER_SIZE - 1);

        sample_t wetL = (dL1 + dR2) * 0.5f;
        sample_t wetR = (dR3 + dL2) * 0.5f;

        // BBD Post-Filter (Reconstruction/Anti-aliasing LPF)
        bbd_lpf_L += 0.35f * (wetL - bbd_lpf_L);
        bbd_lpf_R += 0.35f * (wetR - bbd_lpf_R);

        sample_t outL = inL * (1.0f - tMix) + bbd_lpf_L * tMix;
        sample_t outR = inR * (1.0f - tMix) + bbd_lpf_R * tMix;

        left[i] = outL * tVol;
        right[i] = outR * tVol;
    }
}

uint8_t ChorusEffect::getParamCount() const { return 4; }

ParamInfo ChorusEffect::getParamInfo(uint8_t index) const {
    switch(index) {
        case 0: return {"Rate", 0.1f, 10.0f, "%.2f Hz", 0.1f, ParamCurve::LINEAR};
        case 1: return {"Depth", 0.0f, 1.0f, "%.2f", 0.05f, ParamCurve::LINEAR};
        case 2: return {"Mix", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        case 3: return {"Volume", 0.0f, 2.0f, "%.2f", 0.1f, ParamCurve::LINEAR};
        default: return {"", 0.0f, 0.0f, "", 0.0f, ParamCurve::LINEAR};
    }
}

sample_t ChorusEffect::getParamValue(uint8_t index) const {
    switch(index) {
        case 0: return targetRate.load(std::memory_order_relaxed);
        case 1: return targetDepth.load(std::memory_order_relaxed);
        case 2: return targetMix.load(std::memory_order_relaxed);
        case 3: return targetVolume.load(std::memory_order_relaxed);
        default: return 0.0f;
    }
}

void ChorusEffect::setParamValue(uint8_t index, sample_t value) {
    switch(index) {
        case 0: targetRate.store(std::clamp(value, 0.1f, 10.0f), std::memory_order_relaxed); break;
        case 1: targetDepth.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); break;
        case 2: targetMix.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); break;
        case 3: targetVolume.store(std::clamp(value, 0.0f, 2.0f), std::memory_order_relaxed); break;
    }
    recalculateCoefficients();
}
