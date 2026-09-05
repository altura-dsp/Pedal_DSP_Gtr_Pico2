#pragma once
#include "DSPTypes.h"
#include <string.h>

#include <cmath>
#include "FastMath.h"

class SlotSafetyLimiter {
private:
    sample_t lpfZ1_L{0.0f};
    sample_t lpfZ1_R{0.0f};
    sample_t lpfAlpha{1.0f};
    const sample_t bias{0.05f};
    sample_t biasClipOffset;

    inline sample_t softClip(sample_t x) {
        return x / (1.0f + std::abs(x) * 0.85f);
    }

public:
    // Passthrough/Unity Gain compliant (Regla 12)
    SlotSafetyLimiter() {
        biasClipOffset = softClip(bias);
    }

    void init(sample_t sampleRate) {
        // FIX-1 (P0 vptr bug): memset eliminado
        sample_t w = 2.0f * M_PI * 18000.0f / sampleRate;
        lpfAlpha = 1.0f - FastMath::fast_expf(-w);
    }

    inline sample_t processL(sample_t sample) {
        // Saturación Asimétrica
        sample = softClip(sample + bias) - biasClipOffset;
        // Anti-aliasing LPF 18kHz
        lpfZ1_L += lpfAlpha * (sample - lpfZ1_L);
        sample = lpfZ1_L;
        // Hard Clip @ 99%
        return std::fmin(std::fmax(sample, -0.99f), 0.99f);
    }

    inline sample_t processR(sample_t sample) {
        sample = softClip(sample + bias) - biasClipOffset;
        lpfZ1_R += lpfAlpha * (sample - lpfZ1_R);
        sample = lpfZ1_R;
        return std::fmin(std::fmax(sample, -0.99f), 0.99f);
    }

    inline void processBlock(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
        for (size_t i = 0; i < numSamples; ++i) {
            left[i] = processL(left[i]);
            right[i] = processR(right[i]);
        }
    }
};
