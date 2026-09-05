#pragma once
#include "DSPTypes.h"
#include <string.h>

#include "IEffect.h"

// Signature of a boutique per-sample process function
typedef void (*EffectProcessLegacy)(void* effectInstance, sample_t* inL, sample_t* inR, sample_t* outL, sample_t* outR);

class IEffectWrapper final : public IEffect {
private:
    void* legacyEffect;
    EffectProcessLegacy legacyProcess;

public:
    // Passthrough/Unity Gain compliant (Regla 12)
    IEffectWrapper(void* effect, EffectProcessLegacy processFn) 
        : legacyEffect(effect), legacyProcess(processFn) {}

    void init(sample_t sampleRate) override { }
    void reset() override { }
    void setEnabled(bool state) override { }
    bool isEnabled() const override { return true; }
    uint8_t getParamCount() const override { return 0; }
    ParamInfo getParamInfo(uint8_t index) const override { return {"", 0, 0, "", 0, ParamCurve::LINEAR}; }
    sample_t getParamValue(uint8_t index) const override { return 0.0f; }
    void setParamValue(uint8_t index, sample_t value) override { }

    // Este método permite a la arquitectura heredada llamar a un proceso que usa referencias internas
    inline void processBlock(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) override {
        for (size_t i = 0; i < numSamples; ++i) {
            sample_t& inL = left[i];
            sample_t& inR = right[i];
            sample_t outL = inL;
            sample_t outR = inR;
            
            legacyProcess(legacyEffect, &inL, &inR, &outL, &outR);

            left[i] = outL;
            right[i] = outR;
        }
    }
};
