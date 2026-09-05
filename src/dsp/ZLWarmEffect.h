#pragma once
#include "DSPTypes.h"
#include <string.h>

#include "IEffect.h"
#include <atomic>

/**
 * @brief ZLWarm (Master Warming / Inflator) FPU
 * 
 * Implementación puramente flotante y RT-Safe del clon de Oxford Inflator.
 * Excita los armónicos para maximizar el volumen percibido sin destrozar
 * la dinámica general de la señal.
 * - Parameter Shadowing atómico.
 * - Sin branching ni saltos abruptos.
 */
class ZLWarmEffect final : public IEffect {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    ZLWarmEffect();
    ~ZLWarmEffect() override = default;

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

    // Parámetros UI
    std::atomic<sample_t> curve;
    std::atomic<sample_t> warm;
    std::atomic<sample_t> wet;

    struct ZLWarmCoeffs {
        sample_t p_a, p_b, p_c, p_d, p_comp;
        sample_t n_a, n_b, n_c, n_d, n_comp;
        sample_t warm;
        sample_t wet;
    };
    ZLWarmCoeffs coeffs[2];
    std::atomic<uint8_t> activeCoeffsIdx{0};

    void recalculateCoefficients();
    inline sample_t shape(sample_t x, sample_t w, sample_t pa, sample_t pb, sample_t pc, sample_t pd, sample_t pcomp, sample_t na, sample_t nb, sample_t nc, sample_t nd, sample_t ncomp) const;
};
