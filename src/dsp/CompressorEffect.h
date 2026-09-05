#pragma once
#include "DSPTypes.h"
#include <string.h>

#include "IEffect.h"
#include <atomic>

/**
 * @brief CompressorEffect FPU (Fase 3 - Professional Grade)
 * 
 * Implementación de compresor feed-forward en Float32 puro.
 * - Parameter Shadowing (Lecturas atómicas únicas por bloque).
 * - Algoritmo RMS en Float32.
 * - Interpolación exponencial de envolvente matemática exacta.
 */
class CompressorEffect final : public IEffect {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    CompressorEffect();
    ~CompressorEffect() override = default;

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

    // Variables de Estado (DSP Thread local, NO atómicas)
    sample_t env_sq_avg;
    sample_t current_gain;

    // Parámetros UI Atómicos para Thread-Safety
    std::atomic<sample_t> thresholdDb;
    std::atomic<sample_t> ratio;
    std::atomic<sample_t> attackMs;
    std::atomic<sample_t> releaseMs;
    std::atomic<sample_t> makeupDb;

    struct CompCoeffs {
        sample_t threshold_linear;
        sample_t ratio_coeff;
        sample_t makeup_linear;
        sample_t alpha_attack;
        sample_t alpha_release;
    };
    CompCoeffs coeffs[2];
    std::atomic<uint8_t> activeCoeffsIdx{0};

    void recalculateCoefficients();

    // Constantes
    sample_t alpha_rms;
};
