#pragma once
#include "DSPTypes.h"
#include <string.h>

#include "IEffect.h"
#include <atomic>

#define FLANGER_BUFFER_SIZE 512 // 10.6ms a 48kHz (Potencia de 2 obligatoria para & mask)

/**
 * @brief FlangerEffect FPU (Fase 7)
 * 
 * Flanger mono-procesado, salida estéreo.
 * - Delay ultra-corto (0.1ms a 8ms).
 * - Buffer de 384 muestras que cabe en los 2048 bytes del Slot.
 * - Interpolación Fractional Delay de Thiran 1er Orden.
 */
class FlangerEffect final : public IEffect {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    FlangerEffect();
    ~FlangerEffect() override = default;

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

    struct FlangerCoeffs {
        sample_t rate;
        sample_t depth;
        sample_t feedback;
        sample_t mix;
    };
    FlangerCoeffs coeffs[2];
    std::atomic<uint8_t> activeCoeffsIdx{0};

    void recalculateCoefficients();
    
    sample_t lfoPhase;
    uint32_t writeIndex;
    sample_t thiranZ1;

    // Buffer principal (384 * 4 = 1536 bytes)
    sample_t buffer[FLANGER_BUFFER_SIZE];

    static constexpr sample_t SMOOTH_COEF = 0.005f;
};
