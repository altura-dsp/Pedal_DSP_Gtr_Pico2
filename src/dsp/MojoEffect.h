// Mojo wavefolder adapted from "Mojo" by Chris Johnson (Airwindows), MIT License.
// Ported and modified for RP2350 DSP. Modifications: GPLv3 (see LICENSE).
#pragma once
#include "DSPTypes.h"
#include <string.h>

#include "IEffect.h"
#include <atomic>
#include <algorithm>

/**
 * @brief Efecto MOJO (Wavefolder Boutique FPU)
 * 
 * Basado en algoritmos de Airwindows. Utiliza matemática asintótica en
 * punto flotante (FPU del RP2350) para "doblar" las formas de onda
 * sobre un umbral.
 * - Parameter Shadowing.
 * - Block Processing adaptado.
 */
class MojoEffect final : public IEffect {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    MojoEffect();

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

    // Parameter Shadowing
    std::atomic<sample_t> targetMojo{0.5f};
    std::atomic<sample_t> targetMix{1.0f};

    struct MojoCoeffs {
        sample_t mojo;
        sample_t mix;
    };
    MojoCoeffs coeffs[2];
    std::atomic<uint8_t> activeCoeffsIdx{0};

    void recalculateCoefficients();
};
