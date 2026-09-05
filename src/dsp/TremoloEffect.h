#pragma once
#include "DSPTypes.h"
#include <string.h>

#include "IEffect.h"
#include <atomic>

/**
 * @brief TremoloEffect FPU (Fase 7)
 * 
 * Implementación de Trémolo Estándar y Armónico (Brownface) en Float32.
 * - Interpolación LFO desde Flash (LfoTablesFloat).
 * - Slew-Rate Limiting en Rate y Depth (Anti-zipper).
 * - Crossover 1er orden para modo Armónico.
 */
class TremoloEffect final : public IEffect {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    TremoloEffect();
    ~TremoloEffect() override = default;

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
    std::atomic<sample_t> targetShape;    // 0 = Sine, 1 = Square
    std::atomic<sample_t> targetHarmonic; // 0 = Standard, 1 = Harmonic (Brownface)

    struct TremoloCoeffs {
        sample_t rate;
        sample_t depth;
        sample_t shape;
        sample_t harmonic;
    };
    TremoloCoeffs coeffs[2];
    std::atomic<uint8_t> activeCoeffsIdx{0};

    void recalculateCoefficients();

    // Variables de Estado (DSP local)
    sample_t lfoPhase;

    // Filtros para Trémolo Armónico (frecuencia de cruce ~400Hz)
    sample_t lpfStateL;
    sample_t lpfStateR;
    sample_t crossoverAlpha;

    static constexpr sample_t SMOOTH_COEF = 0.005f; // Para suavizado anti-zipper
};
