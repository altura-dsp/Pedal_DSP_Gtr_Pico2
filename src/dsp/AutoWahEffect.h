#pragma once
#include "DSPTypes.h"
#include <string.h>

#include "IEffect.h"
#include <atomic>

/**
 * @brief AutoWahEffect FPU
 * 
 * Efecto Auto-Wah basado en envelope follower + filtro SVF TPT en float32 nativo.
 */
class AutoWahEffect final : public IEffect {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    AutoWahEffect();
    ~AutoWahEffect() override = default;

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
    std::atomic<sample_t> targetSensitivity;
    std::atomic<sample_t> targetAttack;
    std::atomic<sample_t> targetRelease;
    std::atomic<sample_t> targetQ;

    struct AutoWahCoeffs {
        sample_t sensitivity;
        sample_t q;
        sample_t attackCoef;
        sample_t releaseCoef;
    };
    AutoWahCoeffs coeffs[2];
    std::atomic<uint8_t> activeCoeffsIdx{0};

    void recalculateCoefficients();

    // Envelope Follower
    sample_t envelope;

    // Estado del SVF
    sample_t z1_L, z2_L;
    sample_t z1_R, z2_R;

    static constexpr sample_t SMOOTH_COEF = 0.005f;

    void recalculateTimeConstants();
};
