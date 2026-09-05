#pragma once
#include "DSPTypes.h"
#include <string.h>

#include "IEffect.h"
#include <atomic>

/**
 * @brief Simulador de Guitarra Acústica (Basado en Resonador Modal)
 * 
 * Utiliza una red de filtros resonantes para emular la resonancia mecánica
 * de un cuerpo de madera.
 */
class AcousticSimEffect final : public IEffect {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    AcousticSimEffect();
    ~AcousticSimEffect() override = default;
    
    void init(sample_t sampleRate) override;
    void processBlock(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) override;
    void reset() override;
    
    void setEnabled(bool state) override { enabled = state; }
    bool isEnabled() const override { return enabled; }

    // FASE SOLID: Interfaz genérica de parámetros
    uint8_t getParamCount() const override;
    ParamInfo getParamInfo(uint8_t index) const override;
    sample_t getParamValue(uint8_t index) const override;
    void setParamValue(uint8_t index, sample_t value) override;

private:
    bool enabled;
    sample_t sampleRate_;

    std::atomic<sample_t> targetBody;
    std::atomic<sample_t> targetBrilliance;
    
    struct AcousticState {
        sample_t state1 = 0.0f, state2 = 0.0f;
    } states[4];

    struct AcousticCoeffs {
        sample_t f_coeff;
        sample_t damp;
        sample_t compFactor;
    };
    AcousticCoeffs coeffs[2][4];
    std::atomic<uint8_t> activeCoeffsIdx{0};

    void recalculateCoefficients();
    inline sample_t processSVF(sample_t in, const AcousticCoeffs& c, AcousticState& s);
};
