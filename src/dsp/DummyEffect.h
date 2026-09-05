#pragma once
#include "DSPTypes.h"
#include <string.h>

#include "IEffect.h"

/**
 * @brief DummyEffect para la Fase 2 (Memory Pool)
 * 
 * Actúa como un esqueleto para poder instanciar cualquier efecto en el Memory Pool
 * mediante Placement New antes de que sean portados oficialmente a FPU en la Fase 4.
 */
class DummyEffect final : public IEffect {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    void init(sample_t sampleRate) override { (void)sampleRate; }
    
    // Procesamiento "Bypass" Transparente
    inline void processBlock(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) override {
        (void)left;
        (void)right;
        (void)numSamples;
    }
    
    void reset() override {}
    void setEnabled(bool state) override { enabled = state; }
    bool isEnabled() const override { return enabled; }
    
    uint8_t getParamCount() const override { return 0; }
    ParamInfo getParamInfo(uint8_t index) const override { 
        (void)index;
        return ParamInfo{"Dummy", 0.0f, 1.0f, "%.1f", 0.1f}; 
    }
    sample_t getParamValue(uint8_t index) const override { (void)index; return 0.0f; }
    void setParamValue(uint8_t index, sample_t value) override { (void)index; (void)value; }

private:
    bool enabled = true;
};
