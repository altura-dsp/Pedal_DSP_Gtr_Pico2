#pragma once
#include "DSPTypes.h"
#include <string.h>

#include "IEffect.h"
#include <atomic>

/**
 * @brief Noise Gate Pro FPU (Fase 4 - Professional Grade)
 * 
 * Puerta de ruido inteligente con Hysteresis.
 * Implementación 100% Float32 con Parameter Shadowing.
 */
class NoiseGateEffect final : public IEffect {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    NoiseGateEffect();
    ~NoiseGateEffect() override = default;

    void init(sample_t sampleRate) override;
    void processBlock(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) override;
    void reset() override;

    void setEnabled(bool state) override { enabled.store(state, std::memory_order_relaxed); }
    bool isEnabled() const override { return enabled.load(std::memory_order_relaxed); }

    // FASE SOLID: Interfaz genérica de parámetros
    uint8_t getParamCount() const override;
    ParamInfo getParamInfo(uint8_t index) const override;
    sample_t getParamValue(uint8_t index) const override;
    void setParamValue(uint8_t index, sample_t value) override;

private:
    std::atomic<bool> enabled;
    sample_t sampleRate_;

    // Variables de Estado (DSP Thread local)
    sample_t env_sq_avg;
    sample_t current_gain;
    bool gate_open;

    // Parámetros UI Atómicos
    std::atomic<sample_t> thresholdDb;

    struct NoiseGateCoeffs {
        sample_t threshold_open_linear;
        sample_t threshold_close_linear;
    };
    NoiseGateCoeffs coeffs[2];
    std::atomic<uint8_t> activeCoeffsIdx{0};

    void recalculateCoefficients();
    
    // Constantes
    sample_t alpha_rms;
    sample_t alpha_release;

    void updateThreshold();
};
