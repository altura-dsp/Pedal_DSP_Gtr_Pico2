#pragma once
#include "DSPTypes.h"
#include <string.h>

#include <atomic>
#include <cmath>
#include "FastMath.h"
#include "../audio/MathUtils.h"
// We need a SmoothedValue class. I will assume it exists in MathUtils or I will implement a minimal one if needed.
// Actually, the audit mentioned "LPF Exponencial de Aguilar". I'll implement a simple LPF here if SmoothedValue isn't available.

class SmoothedValueLPF {
private:
    sample_t current{1.0f};
    sample_t target{1.0f};
    sample_t coeff{1.0f};
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    SmoothedValueLPF(sample_t initialValue = 1.0f) : current(initialValue), target(initialValue) {}
    void setSettleTime(sample_t ms, sample_t sampleRate) {
        coeff = FastMath::fast_expf(-2.0f * M_PI * (1000.0f / ms) / sampleRate);
    }
    void setTargetValue(sample_t newTarget) { target = newTarget; }
    sample_t getNextValue() {
        current += (target - current) * (1.0f - coeff);
        return current;
    }
};

class SweetSpotConditioner {
private:
    std::atomic<sample_t> targetGain{1.0f};
    SmoothedValueLPF smoothedGain{1.0f};
    bool bypassEnabled = false;
    bool wasCalibrated = false;

    // Variables DC Blocker (1-polo IIR) a 32-bits sample_t
    sample_t dcBlockerZ1_L{0.0f};
    sample_t dcBlockerZ1_R{0.0f};
    sample_t dcAlpha{0.995f};
    bool isFirstSample{true};

    inline sample_t processDCBlockerL(sample_t sample) {
        sample_t out = sample - dcBlockerZ1_L;
        dcBlockerZ1_L = sample + dcAlpha * dcBlockerZ1_L;
        return out;
    }
    inline sample_t processDCBlockerR(sample_t sample) {
        sample_t out = sample - dcBlockerZ1_R;
        dcBlockerZ1_R = sample + dcAlpha * dcBlockerZ1_R;
        return out;
    }

    // Ring Buffer para Calibración Asíncrona
    static constexpr int RB_SIZE = 2048; // Potencia de 2
    sample_t rbDataL[RB_SIZE];
    sample_t rbDataR[RB_SIZE];
    std::atomic<int> rbHead{0};
    std::atomic<int> rbTail{0};

    void pushCalibrationSample(sample_t l, sample_t r) {
        int head = rbHead.load(std::memory_order_relaxed);
        int nextHead = (head + 1) & (RB_SIZE - 1);
        if (nextHead != rbTail.load(std::memory_order_acquire)) {
            rbDataL[head] = l;
            rbDataR[head] = r;
            std::atomic_thread_fence(std::memory_order_release);
            rbHead.store(nextHead, std::memory_order_relaxed);
        }
    }

public:
    // Passthrough/Unity Gain compliant (Regla 12)
    SweetSpotConditioner() {}
    
    void init(sample_t sampleRate) {
        // FIX-1 (P0 vptr bug): memset eliminado
        smoothedGain.setSettleTime(50.0f, sampleRate); // 50ms rampa exponencial
        dcAlpha = FastMath::fast_expf(-2.0f * M_PI * 20.0f / sampleRate); // 20Hz HPF dinamico
    }

    inline void processAsyncCalibrationCore1() {
        int tail = rbTail.load(std::memory_order_relaxed);
        int head = rbHead.load(std::memory_order_acquire);
        if (tail == head) return;
        
        sample_t sumSqL = 0.0f;
        sample_t sumSqR = 0.0f;
        int count = 0;
        
        while (tail != head) {
            sample_t l = rbDataL[tail];
            sample_t r = rbDataR[tail];
            sumSqL += l * l;
            sumSqR += r * r;
            tail = (tail + 1) & (RB_SIZE - 1);
            count++;
        }
        rbTail.store(tail, std::memory_order_release);
        
        if (count == 0) return;
        sample_t inputRMS_L = std::sqrt(sumSqL / count);
        sample_t inputRMS_R = std::sqrt(sumSqR / count);

        calibrate(inputRMS_L, inputRMS_R);
    }

    void calibrate(sample_t inputRMS_L, sample_t inputRMS_R) {
        sample_t maxRMS = fmaxf(inputRMS_L, inputRMS_R);
        
        if (maxRMS > 0.50f) {  // > -6dBFS
            std::atomic_thread_fence(std::memory_order_release);
            if (maxRMS > 0.70f) {
                targetGain.store(0.70f * (1.0f / maxRMS), std::memory_order_relaxed);
            } else {
                targetGain.store(1.0f, std::memory_order_relaxed);
            }
        } else {
            sample_t neededGain = fminf(0.70f * (1.0f / (maxRMS + 1e-6f)), 10.0f); // Max +20dB
            if (!std::isfinite(neededGain) || neededGain < 0.0f) neededGain = 1.0f;
            std::atomic_thread_fence(std::memory_order_release);
            targetGain.store(neededGain, std::memory_order_relaxed);
        }
        wasCalibrated = true;
    }

    void setBypass(bool state) { bypassEnabled = state; }
    bool isCalibrated() const { return wasCalibrated; }

    inline void processBlock(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
        if (bypassEnabled) return;

        // Sincronización atómica segura por bloque
        sample_t currentGain = targetGain.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        smoothedGain.setTargetValue(currentGain);

        for (size_t i = 0; i < numSamples; ++i) {
            sample_t gain = smoothedGain.getNextValue();
            sample_t sampleL = left[i] * gain;
            sample_t sampleR = right[i] * gain;
            
            if (isFirstSample) {
                dcBlockerZ1_L = sampleL;
                dcBlockerZ1_R = sampleR;
                isFirstSample = false;
            }
            
            left[i] = processDCBlockerL(sampleL);
            right[i] = processDCBlockerR(sampleR);
            
            // Enviar a Core 1 para calibración asíncrona
            pushCalibrationSample(left[i], right[i]);
        }
    }
};
