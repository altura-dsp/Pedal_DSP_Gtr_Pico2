#pragma once
#include "DSPTypes.h"
#include <string.h>

#include "IEffect.h"
#include <atomic>

/**
 * @brief WahEffect FPU
 * 
 * Implementación de efecto Wah usando Filtro State Variable Filter (SVF)
 * Topology Preserving Transform (TPT) en float32 nativo.
 */
class WahEffect final : public IEffect {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    WahEffect();
    ~WahEffect() override = default;

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
    std::atomic<sample_t> targetPosition; // 0.0 a 1.0
    std::atomic<sample_t> targetQ;        // 0.0 a 1.0

    // Variables de Estado (DSP local)
    // Estado del SVF
    sample_t z1_L = 0.0f, z2_L = 0.0f;
    sample_t z1_R = 0.0f, z2_R = 0.0f;

    struct WahCoeffs {
        sample_t g;
        sample_t g1;
        sample_t d;
    };
    WahCoeffs coeffs[2];
    std::atomic<uint8_t> activeCoeffsIdx{0};

    void recalculateCoefficients();
};
