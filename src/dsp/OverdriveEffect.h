#pragma once
#include "DSPTypes.h"
#include <string.h>

#include "IEffect.h"
#include <atomic>
#include <algorithm>

/**
 * @brief Efecto de Overdrive Boutique (FPU / Block Processing)
 * 
 * Basado en el modelado de un Tube Screamer / Blues Driver.
 * Características:
 * - Clipping polinómico de 3er orden (Soft saturation) 100% Float32.
 * - Pre-EQ (HPF) para mantener los bajos definidos.
 * - Post-EQ (LPF) para eliminar armónicos irritantes.
 * - Oversampling 2x integrado.
 * - Parameter Shadowing atómico.
 */
class OverdriveEffect final : public IEffect {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    OverdriveEffect();

    void init(sample_t sampleRate) override;
    void processBlock(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) override;
    void reset() override;

    void setEnabled(bool state) override { enabled = state; }
    bool isEnabled() const override { return enabled; }

    uint8_t getParamCount() const override;
    ParamInfo getParamInfo(uint8_t index) const override;
    sample_t getParamValue(uint8_t index) const override;
    void setParamValue(uint8_t index, sample_t value) override;

private:
    std::atomic<bool> enabled;
    sample_t sampleRate_;

    // Parameter Shadowing (Lock-Free)
    std::atomic<sample_t> targetGain{0.5f};
    std::atomic<sample_t> targetTone{0.5f};
    std::atomic<sample_t> targetLevel{0.5f};

    struct OverdriveCoeffs {
        sample_t gain;
        sample_t tone_alpha;
        sample_t level;
    };
    OverdriveCoeffs coeffs[2];
    std::atomic<uint8_t> activeCoeffsIdx{0};

    void recalculateCoefficients();

    // Estados de filtros (L y R)
    sample_t preHPF_sL, preHPF_sR;
    sample_t postLPF_sL, postLPF_sR;

    sample_t preHPF_alpha;

    // Oversampling state
    sample_t prevXL, prevXR;
    sample_t prevDistL, prevDistR;

    inline sample_t softClip(sample_t x) {
        // Polinomio x - x^3/3 (Saturación suave)
        x = std::clamp(x, -1.5f, 1.5f);
        return x - (x * x * x) * 0.33333333f;
    }
};
