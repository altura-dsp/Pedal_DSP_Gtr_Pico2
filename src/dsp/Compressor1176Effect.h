#pragma once
#include "DSPTypes.h"
#include <string.h>

#include "IEffect.h"
#include <atomic>

/**
 * @brief Compressor1176Effect FPU
 * 
 * Compresor FET estilo 1176.
 * Utiliza aproximaciones matemáticas (Taylor) para la curva Shockley
 * evitando funciones costosas como expf() por sample.
 */
class Compressor1176Effect final : public IEffect {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    Compressor1176Effect();
    ~Compressor1176Effect() override = default;

    void init(sample_t sampleRate) override;
    void processBlock(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) override;
    void reset() override;

    void setEnabled(bool state) override { enabled.store(state, std::memory_order_relaxed); }
    bool isEnabled() const override { return enabled.load(std::memory_order_relaxed); }

    uint8_t getParamCount() const override;
    ParamInfo getParamInfo(uint8_t index) const override;
    sample_t getParamValue(uint8_t index) const override;
    void setParamValue(uint8_t index, sample_t value) override;

private:
    std::atomic<bool> enabled;
    sample_t sampleRate_;

    // Parámetros Atómicos (UI)
    std::atomic<sample_t> targetInputGain;  // 0.0 a 1.0 (mapeado a dB)
    std::atomic<sample_t> targetOutputGain; // 0.0 a 1.0 (mapeado a dB)
    std::atomic<sample_t> targetAttack;     // 1 a 7 (20us a 800us invertido)
    std::atomic<sample_t> targetRelease;    // 1 a 7 (50ms a 1100ms invertido)
    std::atomic<sample_t> targetRatio;      // 0=4:1, 1=8:1, 2=12:1, 3=20:1, 4=AllButtonsIn

    // Estado DSP
    sample_t currentInputGain;
    sample_t currentOutputGain;
    sample_t envelope;
    
    struct Comp1176Coeffs {
        sample_t attackCoef;
        sample_t releaseCoef;
        sample_t targetInGainLin;
        sample_t targetOutGainLin;
        sample_t ratioIdx;
    };
    Comp1176Coeffs coeffs[2];
    std::atomic<uint8_t> activeCoeffsIdx{0};

    void recalculateCoefficients();
};
