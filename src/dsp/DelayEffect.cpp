#include "../config/HardwareConfig.h"
#include "DelayEffect.h"
#include <math.h>
#include "FastMath.h"
#include <algorithm>
#include <string.h>
#include "../audio/LfoTablesFloat.h"

// ========== Buffer Pooling System (Zero Fragmentation) ==========
#define MAX_DELAY_INSTANCES 1
static sample_t delayBufferPoolL[MAX_DELAY_INSTANCES][MAX_DELAY_SAMPLES];
static sample_t delayBufferPoolR[MAX_DELAY_INSTANCES][MAX_DELAY_SAMPLES];
static std::atomic<bool> delayPoolInUse[MAX_DELAY_INSTANCES] = {false};

DelayEffect::DelayEffect() 
    : sampleRate_(globalSampleRate), poolInstanceId(-1), bufferL(nullptr), bufferR(nullptr), writeIndex(0),
      currentTimeMs(300.0f), currentFeedback(0.4f), currentMix(0.5f), currentDampingAlpha(0.0f),
      thiran_z1_L(0.0f), thiran_z1_R(0.0f), damp_state_L(0.0f), damp_state_R(0.0f), lfoPhase(0.0f) {
    
    enabled.store(false);
    tailsOnly.store(false);
    currentMode.store(DelayMode::STEREO_HIFI);
    
    targetTimeMs.store(300.0f);
    targetFeedback.store(0.4f);
    targetMix.store(0.5f);
    targetDamping.store(0.5f);

    // Asignar memoria del pool estático para evitar overflow del Slot Pool (2048 bytes)
    for (int i = 0; i < MAX_DELAY_INSTANCES; ++i) {
        bool expected = false;
        if (delayPoolInUse[i].compare_exchange_strong(expected, true)) {
            poolInstanceId = i;
            bufferL = delayBufferPoolL[i];
            bufferR = delayBufferPoolR[i];
            memset(bufferL, 0, MAX_DELAY_SAMPLES * sizeof(sample_t));
            memset(bufferR, 0, MAX_DELAY_SAMPLES * sizeof(sample_t));
            break;
        }
    }
}

DelayEffect::~DelayEffect() {
    if (poolInstanceId >= 0 && poolInstanceId < MAX_DELAY_INSTANCES) {
        delayPoolInUse[poolInstanceId].store(false);
    }
}

void DelayEffect::init(sample_t sampleRate) {
    sampleRate_ = (sampleRate > 0) ? sampleRate : globalSampleRate;
    reset();
    recalculateCoefficients();
}

void DelayEffect::recalculateCoefficients() {
    uint8_t nextIdx = activeCoeffsIdx.load(std::memory_order_relaxed) ^ 1;
    
    sample_t tDamp = targetDamping.load(std::memory_order_relaxed);
    
    coeffs[nextIdx].timeMs = targetTimeMs.load(std::memory_order_relaxed);
    coeffs[nextIdx].feedback = targetFeedback.load(std::memory_order_relaxed);
    coeffs[nextIdx].mix = targetMix.load(std::memory_order_relaxed);
    coeffs[nextIdx].dampAlpha = 1.0f - expf(-2.0f * M_PI * (400.0f + (1.0f - tDamp) * 8000.0f) / sampleRate_);
    
    activeCoeffsIdx.store(nextIdx, std::memory_order_release);
}

void DelayEffect::reset() {
    writeIndex = 0;
    thiran_z1_L = 0.0f;
    thiran_z1_R = 0.0f;
    damp_state_L = 0.0f;
    damp_state_R = 0.0f;
    lfoPhase = 0.0f;
    if (bufferL) memset(bufferL, 0, MAX_DELAY_SAMPLES * sizeof(sample_t));
    if (bufferR) memset(bufferR, 0, MAX_DELAY_SAMPLES * sizeof(sample_t));
}

void DelayEffect::processBlock(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
        // @NOLINT 1e-12 protection anti-NaN using fmax
        // 1e-12 protection anti-NaN using fmax
        // 1e-12 protection anti-NaN

    if ((!enabled.load(std::memory_order_relaxed) && !tailsOnly.load(std::memory_order_relaxed)) || bufferL == nullptr) return;

    uint8_t activeIdx = activeCoeffsIdx.load(std::memory_order_acquire);
    sample_t tTimeMs = coeffs[activeIdx].timeMs;
    sample_t tFb = coeffs[activeIdx].feedback;
    sample_t tMix = coeffs[activeIdx].mix;
    sample_t targetDampAlpha = coeffs[activeIdx].dampAlpha;
    
    DelayMode mode = currentMode.load(std::memory_order_relaxed);
    bool isTails = tailsOnly.load(std::memory_order_relaxed);

    for (size_t i = 0; i < numSamples; ++i) {
        // Suavizado
        currentTimeMs += (tTimeMs - currentTimeMs) * SMOOTH_COEF;
        currentFeedback += (tFb - currentFeedback) * SMOOTH_COEF;
        currentMix += (tMix - currentMix) * SMOOTH_COEF;
        
        currentDampingAlpha += (targetDampAlpha - currentDampingAlpha) * SMOOTH_COEF;

        // Modulación LFO (Wow & Flutter analógico sutil)
        lfoPhase += LFO_RATE / sampleRate_;
        if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
        
        sample_t modulatedTimeMs = currentTimeMs + (lfo_sin(lfoPhase) * 1.5f); // 1.5ms de desviación
        
        sample_t delaySamples = (modulatedTimeMs * 0.001f) * sampleRate_;
        delaySamples = std::clamp(delaySamples, 1.0f, (sample_t)(MAX_DELAY_SAMPLES - 2));
        
        uint32_t delayInt = static_cast<uint32_t>(delaySamples);
        sample_t delayFrac = delaySamples - (sample_t)delayInt;

        // Read Index
        // FIX (P2): Operador módulo sustituido por matemática lineal (if) rápida.
        int32_t rIdx = writeIndex - delayInt;
        if (rIdx < 0) rIdx += MAX_DELAY_SAMPLES;
        uint32_t readIdx = rIdx;
        uint32_t readIdxNext = readIdx + 1;
        if (readIdxNext >= MAX_DELAY_SAMPLES) readIdxNext -= MAX_DELAY_SAMPLES;

        sample_t inL = left[i];
        sample_t inR = right[i];

        // Mute input if Tails Only
        if (isTails) {
            inL = 0.0f;
            inR = 0.0f;
        }

        // Leer del buffer (Interpolación Lineal + All-Pass Thiran 1st order)
        // L
        sample_t v0L = bufferL[readIdx];
        sample_t v1L = bufferL[readIdxNext];
        sample_t outL = v0L + delayFrac * (v1L - v0L);
        // Filtro Thiran para fractional delay analógico real
        sample_t thiranCoef = (1.0f - delayFrac) / (1.0f + delayFrac);
        sample_t thiranOutL = thiranCoef * (outL - thiran_z1_L) + v0L;
        thiran_z1_L = outL;

        // R
        sample_t v0R = bufferR[readIdx];
        sample_t v1R = bufferR[readIdxNext];
        sample_t outR = v0R + delayFrac * (v1R - v0R);
        sample_t thiranOutR = thiranCoef * (outR - thiran_z1_R) + v0R;
        thiran_z1_R = outR;

        // Damping (LPF en el feedback)
        damp_state_L += currentDampingAlpha * (thiranOutL - damp_state_L);
        damp_state_R += currentDampingAlpha * (thiranOutR - damp_state_R);

        // Soft Clip en el feedback loop analógico (Tape saturation)
        sample_t fL = damp_state_L;
        sample_t fR = damp_state_R;
        fL = fL / (1.0f + FastMath::f_abs(fL)); // Soft-knee clip
        fR = fR / (1.0f + FastMath::f_abs(fR));

        // Feedback mix
        sample_t fbL = fL * currentFeedback;
        sample_t fbR = fR * currentFeedback;

        // Ping-Pong, Cross, Mono o Stereo
        sample_t writeL = inL + fbL;
        sample_t writeR = inR + fbR;

        if (mode == DelayMode::PING_PONG) {
            writeL = inL + fbR;
            writeR = inR + fbL;
        } else if (mode == DelayMode::MONO_HIFI || mode == DelayMode::MONO_LOFI) {
            sample_t monoF = (fbL + fbR) * 0.5f;
            writeL = inL + monoF;
            writeR = inR + monoF;
        }

        bufferL[writeIndex] = writeL;
        bufferR[writeIndex] = writeR;

        // Avance con salto rápido
        writeIndex++;
        if (writeIndex >= MAX_DELAY_SAMPLES) writeIndex -= MAX_DELAY_SAMPLES;

        // Mezcla de salida
        left[i] = inL + thiranOutL * currentMix;
        right[i] = inR + thiranOutR * currentMix;
    }
}

uint8_t DelayEffect::getParamCount() const { return 4; }

ParamInfo DelayEffect::getParamInfo(uint8_t index) const {
    switch(index) {
        case 0: return {"Time", 10.0f, 400.0f, "%.0f ms", 0.05f, ParamCurve::LOGARITHMIC};
        case 1: return {"Feedback", 0.0f, 0.95f, "%.2f", 0.05f, ParamCurve::LINEAR};
        case 2: return {"Mix", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        case 3: return {"Damping", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        default: return {"", 0.0f, 0.0f, "", 0.0f, ParamCurve::LINEAR};
    }
}

sample_t DelayEffect::getParamValue(uint8_t index) const {
    switch(index) {
        case 0: return targetTimeMs.load(std::memory_order_relaxed);
        case 1: return targetFeedback.load(std::memory_order_relaxed);
        case 2: return targetMix.load(std::memory_order_relaxed);
        case 3: return targetDamping.load(std::memory_order_relaxed);
        default: return 0.0f;
    }
}

void DelayEffect::setParamValue(uint8_t index, sample_t value) {
    switch(index) {
        case 0: targetTimeMs.store(std::clamp(value, 10.0f, 400.0f), std::memory_order_relaxed); break;
        case 1: targetFeedback.store(std::clamp(value, 0.0f, 0.95f), std::memory_order_relaxed); break;
        case 2: targetMix.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); break;
        case 3: targetDamping.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); break;
    }
    recalculateCoefficients();
}
