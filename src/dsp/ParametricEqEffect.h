#pragma once
#include "DSPTypes.h"
#include <string.h>

#include "IEffect.h"
#include <atomic>

/**
 * @brief ParametricEqEffect FPU
 * 
 * EQ Paramétrico de 6 bandas usando SVF TPT flotante.
 * Consta de Low Shelf, 4x Bell/Peak, y High Shelf.
 */
class ParametricEqEffect final : public IEffect {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    ParametricEqEffect();
    ~ParametricEqEffect() override = default;

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

    // 18 Parámetros (6 bandas x 3 params: Freq, Q, Gain)
    // Band 0: Low Shelf
    // Band 1-4: Peak
    // Band 5: High Shelf
    std::atomic<sample_t> targetFreq[6];
    std::atomic<sample_t> targetQ[6];
    std::atomic<sample_t> targetGain[6];

    // Estados de filtros
    struct SVFState {
        sample_t z1_L = 0.0f;
        sample_t z2_L = 0.0f;
        sample_t z1_R = 0.0f;
        sample_t z2_R = 0.0f;
    } states[6];

    // Coeficientes precalculados para evitar math en processBlock
    struct SVFCoeffs {
        sample_t g;
        sample_t R;
        sample_t K;
        sample_t g1;
        sample_t d;
    };
    SVFCoeffs coeffs[2][6];
    std::atomic<uint8_t> activeCoeffsIdx{0};

    void recalculateCoefficients();
};
