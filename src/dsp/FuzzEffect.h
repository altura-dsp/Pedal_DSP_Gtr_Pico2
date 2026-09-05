#pragma once
#include "DSPTypes.h"
#include <string.h>

#include "IEffect.h"
#include <atomic>
#include <algorithm>
#include "../audio/utils/DspMathFast.h"

/**
 * @brief Efecto Fuzz Vintage (FPU / Block Processing)
 * 
 * Basado en el modelado de un Fuzz Face de germanio.
 * Características:
 * - Hard-clipping asimétrico brutal 100% Float32.
 * - Bias ajustable para estrangular la forma de onda (gated effect).
 * - DC Blocker flotante ultra-rápido.
 * - Parameter Shadowing atómico.
 */
class FuzzEffect final : public IEffect {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    FuzzEffect();

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
    std::atomic<sample_t> targetGain{0.5f};
    std::atomic<sample_t> targetBias{0.5f};
    std::atomic<sample_t> targetLevel{0.5f};

    struct FuzzCoeffs {
        sample_t gain;
        sample_t bias;
        sample_t level;
    };
    FuzzCoeffs coeffs[2];
    std::atomic<uint8_t> activeCoeffsIdx{0};

    void recalculateCoefficients();

    // Estado de filtros
    sample_t dcBlockL, dcBlockR;
    sample_t dcBlockAlpha;

    inline sample_t fuzzClip(sample_t x, sample_t bias) {
        // Fuzz Face: Alta ganancia y saturación asimétrica.
        // Reemplazamos el clamp (brickwall duro) por fastTanh escalado,
        // lo que elimina el harshness digital manteniendo el "squash" brutal del fuzz.
        sample_t s = x + bias;
        return dspmath::fastTanh(s * 5.0f) * 0.8f;
    }
};
