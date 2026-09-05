#pragma once
#include "DSPTypes.h"
#include <string.h>

#include "IEffect.h"
#include <atomic>

/**
 * @brief Efecto de Distorsión Boutique (Rat Style) FPU
 * 
 * Implementa un modelo de Hard-Clipping usando Float32 puro.
 * - Oversampling 2x interno adaptado a bloques sample_t.
 * - DC Blocker y LPF integrados.
 * - Parameter Shadowing.
 */
class DistortionEffect final : public IEffect {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    enum class DistortionMode {
        CLASSIC_DIODE,
        WAVENET_SOFTSIGN
    };

    DistortionEffect();
    ~DistortionEffect() override = default;

    void init(sample_t sampleRate) override;
    void processBlock(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) override;
    void reset() override;
    
    void setEnabled(bool state) override { enabled.store(state, std::memory_order_relaxed); }
    bool isEnabled() const override { return enabled.load(std::memory_order_relaxed); }

    // ========== FASE SOLID: Interfaz genérica de parámetros ==========
    uint8_t getParamCount() const override;
    ParamInfo getParamInfo(uint8_t index) const override;
    sample_t getParamValue(uint8_t index) const override;
    void setParamValue(uint8_t index, sample_t value) override;

    void setMode(DistortionMode newMode) { mode.store(newMode, std::memory_order_relaxed); }

private:
    std::atomic<bool> enabled;
    std::atomic<DistortionMode> mode;
    sample_t sampleRate_;
    
    // Parámetros atómicos
    std::atomic<sample_t> targetGain;
    std::atomic<sample_t> targetTone;
    std::atomic<sample_t> targetVolume;
    
    struct DistortionCoeffs {
        sample_t gain;
        sample_t volume;
        sample_t filter_alpha;
        DistortionMode mode;
    };
    DistortionCoeffs coeffs[2];
    std::atomic<uint8_t> activeCoeffsIdx{0};

    void recalculateCoefficients();

    // Estados de filtros y oversampling
    sample_t lpStateL, lpStateR;
    sample_t prevXL, prevXR;
    sample_t prevDistL, prevDistR;
    sample_t dcBlockerL, dcBlockerR;
    sample_t prevInL, prevInR;

    static constexpr sample_t SMOOTH_COEF = 0.002f;

    // Helpers
    inline sample_t diodeClip(sample_t x) const;
    inline sample_t softsignClip(sample_t x) const;
};
