// Guitar Conditioner adapted from "GuitarConditioner" by Chris Johnson (Airwindows), MIT License.
// Ported and modified for RP2350 DSP. Modifications: GPLv3 (see LICENSE).
#pragma once
#include "DSPTypes.h"
#include <string.h>

#include "IEffect.h"

/**
 * @brief GuitarConditioner (Port de Airwindows)
 * 
 * Pre-procesador de entrada diseñado para domar los transitorios duros 
 * de la púa (Slew Limiting) y proporcionar una sensación táctil analógica.
 * Adaptado a bloques sample_t de 32 bits nativos (RP2350).
 */
class GuitarConditionerEffect final : public IEffect {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    GuitarConditionerEffect();
    ~GuitarConditionerEffect() override = default;

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

    // Variables de estado del filtro
    sample_t iirSampleTAL, iirSampleTBL;
    sample_t iirSampleTAR, iirSampleTBR;
    sample_t iirSampleBAL, iirSampleBBL;
    sample_t iirSampleBAR, iirSampleBBR;
    sample_t lastSampleTL, lastSampleTR;
    sample_t lastSampleBL, lastSampleBR;
    
    bool fpFlip;

    // Coeficientes precalculados
    sample_t iirTreble;
    sample_t iirBass;
    sample_t threshTreble;
    sample_t threshBass;

    static constexpr sample_t tightBass = 0.6666666666f;
    static constexpr sample_t tightTreble = -0.3333333333f;
};
