#include "../config/HardwareConfig.h"
#include "ReverbEffect.h"
#include <math.h>
#include <algorithm>
#include <string.h>

#define MAX_REVERB_INSTANCES 2
static sample_t revPool1[MAX_REVERB_INSTANCES][FDN_DL1];
static sample_t revPool2[MAX_REVERB_INSTANCES][FDN_DL2];
static sample_t revPool3[MAX_REVERB_INSTANCES][FDN_DL3];
static sample_t revPool4[MAX_REVERB_INSTANCES][FDN_DL4];
static std::atomic<bool> revPoolInUse[MAX_REVERB_INSTANCES] = {false, false};

ReverbEffect::ReverbEffect() 
    : sampleRate_(globalSampleRate), poolInstanceId(-1),
      dl1(nullptr), dl2(nullptr), dl3(nullptr), dl4(nullptr),
      ptr1(0), ptr2(0), ptr3(0), ptr4(0),
      damp_s1(0.0f), damp_s2(0.0f), damp_s3(0.0f), damp_s4(0.0f) {
    
    enabled.store(false);
    tailsOnly.store(false);

    targetSize.store(0.5f);
    targetDamp.store(0.5f);
    targetMix.store(0.3f);

    for (int i = 0; i < MAX_REVERB_INSTANCES; ++i) {
        bool expected = false;
        if (revPoolInUse[i].compare_exchange_strong(expected, true)) {
            poolInstanceId = i;
            dl1 = revPool1[i];
            dl2 = revPool2[i];
            dl3 = revPool3[i];
            dl4 = revPool4[i];
            memset(dl1, 0, FDN_DL1 * sizeof(sample_t));
            memset(dl2, 0, FDN_DL2 * sizeof(sample_t));
            memset(dl3, 0, FDN_DL3 * sizeof(sample_t));
            memset(dl4, 0, FDN_DL4 * sizeof(sample_t));
            break;
        }
    }
}

ReverbEffect::~ReverbEffect() {
    if (poolInstanceId >= 0 && poolInstanceId < MAX_REVERB_INSTANCES) {
        revPoolInUse[poolInstanceId].store(false);
    }
}

void ReverbEffect::init(sample_t sampleRate) {
    sampleRate_ = (sampleRate > 0) ? sampleRate : globalSampleRate;
    reset();
}

void ReverbEffect::reset() {
    ptr1 = ptr2 = ptr3 = ptr4 = 0;
    damp_s1 = damp_s2 = damp_s3 = damp_s4 = 0.0f;
    if (dl1) memset(dl1, 0, FDN_DL1 * sizeof(sample_t));
    if (dl2) memset(dl2, 0, FDN_DL2 * sizeof(sample_t));
    if (dl3) memset(dl3, 0, FDN_DL3 * sizeof(sample_t));
    if (dl4) memset(dl4, 0, FDN_DL4 * sizeof(sample_t));
}

void ReverbEffect::recalculateCoefficients() {
    uint8_t nextIdx = activeCoeffsIdx.load(std::memory_order_relaxed) ^ 1;
    
    coeffs[nextIdx].size = targetSize.load(std::memory_order_relaxed);
    coeffs[nextIdx].mix = targetMix.load(std::memory_order_relaxed);
    
    sample_t tDampVal = targetDamp.load(std::memory_order_relaxed);
    sample_t freqHz = 2000.0f + (1.0f - tDampVal) * 10000.0f;
    coeffs[nextIdx].filter_alpha = 1.0f - expf(-2.0f * M_PI * freqHz / sampleRate_);
    
    activeCoeffsIdx.store(nextIdx, std::memory_order_release);
}

void ReverbEffect::processBlock(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
    if ((!enabled.load(std::memory_order_relaxed) && !tailsOnly.load(std::memory_order_relaxed)) || dl1 == nullptr) return;

    bool isTails = tailsOnly.load(std::memory_order_relaxed);
    
    uint8_t activeIdx = activeCoeffsIdx.load(std::memory_order_acquire);
    sample_t tSize = coeffs[activeIdx].size;
    sample_t tMix = coeffs[activeIdx].mix;
    sample_t filter_alpha = coeffs[activeIdx].filter_alpha;

    for (size_t i = 0; i < numSamples; ++i) {

        sample_t decay = 0.5f + (tSize * 0.49f); // 0.5 a 0.99

        sample_t inL = left[i];
        sample_t inR = right[i];
        
        if (isTails) {
            inL = 0.0f;
            inR = 0.0f;
        }

        sample_t monoIn = (inL + inR) * 0.5f;

        // Leer líneas
        sample_t out1 = dl1[ptr1];
        sample_t out2 = dl2[ptr2];
        sample_t out3 = dl3[ptr3];
        sample_t out4 = dl4[ptr4];

        // LPF Damping
        damp_s1 += filter_alpha * (out1 - damp_s1); out1 = damp_s1;
        damp_s2 += filter_alpha * (out2 - damp_s2); out2 = damp_s2;
        damp_s3 += filter_alpha * (out3 - damp_s3); out3 = damp_s3;
        damp_s4 += filter_alpha * (out4 - damp_s4); out4 = damp_s4;

        // Householder Feedback Matrix (Difusión óptima)
        // Matriz 4x4 ortogonal para mezclar sin perder energía
        sample_t in_feed1 = 0.5f * ( out1 + out2 + out3 + out4);
        sample_t in_feed2 = 0.5f * ( out1 - out2 + out3 - out4);
        sample_t in_feed3 = 0.5f * ( out1 + out2 - out3 - out4);
        sample_t in_feed4 = 0.5f * ( out1 - out2 - out3 + out4);

        // Feedback con decay
        sample_t write1 = monoIn + in_feed1 * decay;
        sample_t write2 = monoIn + in_feed2 * decay;
        sample_t write3 = monoIn + in_feed3 * decay;
        sample_t write4 = monoIn + in_feed4 * decay;

        // Escribir a delay lines
        dl1[ptr1] = std::clamp(write1, -1.0f, 1.0f);
        dl2[ptr2] = std::clamp(write2, -1.0f, 1.0f);
        dl3[ptr3] = std::clamp(write3, -1.0f, 1.0f);
        dl4[ptr4] = std::clamp(write4, -1.0f, 1.0f);

        // Incrementar punteros
        // FIX (P2): Operador Módulo sustituido por saltos (if)
        ptr1++; if (ptr1 >= FDN_DL1) ptr1 = 0;
        ptr2++; if (ptr2 >= FDN_DL2) ptr2 = 0;
        ptr3++; if (ptr3 >= FDN_DL3) ptr3 = 0;
        ptr4++; if (ptr4 >= FDN_DL4) ptr4 = 0;

        // Mezcla estéreo: L = out1 - out2, R = out3 - out4
        sample_t revL = out1 - out2;
        sample_t revR = out3 - out4;

        left[i] = inL + revL * tMix;
        right[i] = inR + revR * tMix;
    }
}

uint8_t ReverbEffect::getParamCount() const { return 3; }

ParamInfo ReverbEffect::getParamInfo(uint8_t index) const {
    switch(index) {
        case 0: return {"Decay", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        case 1: return {"Damping", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        case 2: return {"Mix", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        default: return {"", 0.0f, 0.0f, "", 0.0f, ParamCurve::LINEAR};
    }
}

sample_t ReverbEffect::getParamValue(uint8_t index) const {
    switch(index) {
        case 0: return targetSize.load(std::memory_order_relaxed);
        case 1: return targetDamp.load(std::memory_order_relaxed);
        case 2: return targetMix.load(std::memory_order_relaxed);
        default: return 0.0f;
    }
}

void ReverbEffect::setParamValue(uint8_t index, sample_t value) {
    switch(index) {
        case 0: targetSize.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); break;
        case 1: targetDamp.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); break;
        case 2: targetMix.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); break;
    }
    recalculateCoefficients();
}
