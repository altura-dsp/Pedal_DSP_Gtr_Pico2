#pragma once
#include "DSPTypes.h"
#include <string.h>

#include "IEffect.h"
#include <atomic>

/**
 * @brief PhaserEffect FPU (Fase 7)
 * 
 * Implementación de Phaser de 4 etapas (All-Pass).
 * - Equal-Power Mixing (Precalculado).
 * - Interpolación LFO desde Flash.
 */
class PhaserEffect final : public IEffect {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    PhaserEffect();
    ~PhaserEffect() override = default;

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
    std::atomic<sample_t> targetRate;
    std::atomic<sample_t> targetDepth;
    std::atomic<sample_t> targetFeedback;
    std::atomic<sample_t> targetMix;

    // Variables de Estado (DSP local)
    sample_t lfoPhase;
    sample_t stateL[4];
    sample_t stateR[4];
    sample_t feedbackL;
    sample_t feedbackR;

    struct PhaserCoeffs {
        sample_t rate;
        sample_t depth;
        sample_t feedback;
        sample_t wetGain;
        sample_t dryGain;
    };
    PhaserCoeffs coeffs[2];
    std::atomic<uint8_t> activeCoeffsIdx{0};

    void recalculateCoefficients();

    static constexpr sample_t SMOOTH_COEF = 0.005f;

    // Helper All-Pass
    inline sample_t apf(sample_t x, sample_t& z1, sample_t c) {
        sample_t y = c * x + z1;
        z1 = x - c * y;
        return y;
    }
};
