#pragma once
#include "DSPTypes.h"
#include <string.h>

#include "IEffect.h"
#include <atomic>

/**
 * @brief EQ Global de Alta Eficiencia (Pre/Post)
 * 
 * Contiene:
 * 1. Pre-EQ (Pickup Corrector): 3 bandas paramétricas para corregir respuesta de pastillas.
 * 2. Post-EQ (Room EQ): LowShelf, Mid Peak, HighShelf para adaptar al amplificador/sala.
 * Implementado con SVF en línea sin instanciar clases pesadas.
 */
class GlobalEQ {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    GlobalEQ();
    ~GlobalEQ() = default;

    void init(sample_t sampleRate);
    
    // Procesa el corrector de pastillas (Input)
    void processInput(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples);
    
    // Procesa la EQ de sala/salida (Output)
    void processOutput(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples);

    void setEnabled(bool state) { enabled = state; }
    bool isEnabled() const { return enabled; }

    // Setters atómicos (Fase SOLID)
    void setInputGain(uint8_t band, sample_t gainDb); // bands: 0=125Hz, 1=500Hz, 2=1.5kHz
    void setOutputGain(uint8_t band, sample_t gainDb); // bands: 0=Low, 1=Mid, 2=High

private:
    bool enabled;
    sample_t sampleRate_;

    // Target gains for smoothing (-12 to +12 dB)
    std::atomic<sample_t> targetInGains[3];
    std::atomic<sample_t> targetOutGains[3];

    struct SVFState {
        sample_t state1L = 0.0f, state2L = 0.0f;
        sample_t state1R = 0.0f, state2R = 0.0f;
    };
    SVFState inStates[3];
    SVFState outStates[3];

    struct SVFCoeffs {
        sample_t freq;
        sample_t q;
        sample_t gain_lin;
        sample_t f_coeff;
        sample_t damp;
    };
    struct EqCoeffs {
        SVFCoeffs in[3];
        SVFCoeffs out[3];
    };
    EqCoeffs coeffs[2];
    std::atomic<uint8_t> activeCoeffsIdx{0};

    void updateCoeffs();

    inline sample_t processSVF_Peak(sample_t in, const SVFCoeffs& c, SVFState& s);
    inline sample_t processSVF_LowShelf(sample_t in, const SVFCoeffs& c, SVFState& s);
    inline sample_t processSVF_HighShelf(sample_t in, const SVFCoeffs& c, SVFState& s);
};
