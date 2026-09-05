#include "../config/HardwareConfig.h"
#include "GlobalEQ.h"
#include <math.h>

GlobalEQ::GlobalEQ() : enabled(true), sampleRate_(globalSampleRate) {
    activeCoeffsIdx.store(0, std::memory_order_relaxed);
    for (int i=0; i<3; i++) {
        targetInGains[i].store(0.0f, std::memory_order_relaxed);
        targetOutGains[i].store(0.0f, std::memory_order_relaxed);
        
        inStates[i].state1L = inStates[i].state2L = 0.0f;
        inStates[i].state1R = inStates[i].state2R = 0.0f;
        
        outStates[i].state1L = outStates[i].state2L = 0.0f;
        outStates[i].state1R = outStates[i].state2R = 0.0f;
    }
}

void GlobalEQ::init(sample_t sampleRate) {
        // FIX-1 (P0 vptr bug): memset eliminado
    sampleRate_ = sampleRate;

    // Inicializar frecuencias y Q en ambos buffers
    sample_t inFreq[3]  = {125.0f, 500.0f, 1500.0f};
    sample_t inQ[3]     = {1.0f, 1.0f, 1.0f};
    sample_t outFreq[3] = {200.0f, 800.0f, 3000.0f};
    sample_t outQ[3]    = {0.707f, 1.0f, 0.707f};

    for(int idx = 0; idx < 2; idx++) {
        for(int i = 0; i < 3; i++) {
            coeffs[idx].in[i].freq = inFreq[i];
            coeffs[idx].in[i].q = inQ[i];
            coeffs[idx].out[i].freq = outFreq[i];
            coeffs[idx].out[i].q = outQ[i];
        }
    }

    updateCoeffs();
}

void GlobalEQ::updateCoeffs() {
    uint8_t nextIdx = activeCoeffsIdx.load(std::memory_order_relaxed) ^ 1;

    for (int i=0; i<3; i++) {
        sample_t inDb = targetInGains[i].load(std::memory_order_relaxed);
        sample_t outDb = targetOutGains[i].load(std::memory_order_relaxed);

        coeffs[nextIdx].in[i].f_coeff = 2.0f * sinf((sample_t)M_PI * coeffs[nextIdx].in[i].freq / sampleRate_);
        coeffs[nextIdx].in[i].damp = 1.0f / coeffs[nextIdx].in[i].q;
        coeffs[nextIdx].in[i].gain_lin = powf(10.0f, inDb / 20.0f);

        coeffs[nextIdx].out[i].f_coeff = 2.0f * sinf((sample_t)M_PI * coeffs[nextIdx].out[i].freq / sampleRate_);
        coeffs[nextIdx].out[i].damp = 1.0f / coeffs[nextIdx].out[i].q;
        coeffs[nextIdx].out[i].gain_lin = powf(10.0f, outDb / 20.0f);
    }
    
    activeCoeffsIdx.store(nextIdx, std::memory_order_release);
}

void GlobalEQ::setInputGain(uint8_t band, sample_t gainDb) {
    if (band < 3) {
        targetInGains[band].store(gainDb, std::memory_order_relaxed);
        updateCoeffs();
    }
}

void GlobalEQ::setOutputGain(uint8_t band, sample_t gainDb) {
    if (band < 3) {
        targetOutGains[band].store(gainDb, std::memory_order_relaxed);
        updateCoeffs();
    }
}

inline sample_t GlobalEQ::processSVF_Peak(sample_t in, const SVFCoeffs& c, SVFState& s) {
    sample_t notch = in - c.damp * s.state1L;
    sample_t low = s.state2L + c.f_coeff * s.state1L;
    sample_t high = notch - low;
    sample_t band = c.f_coeff * high + s.state1L;
    
    s.state1L = band;
    s.state2L = low;
    
    return in + band * (c.gain_lin - 1.0f);
}

inline sample_t GlobalEQ::processSVF_LowShelf(sample_t in, const SVFCoeffs& c, SVFState& s) {
    sample_t notch = in - c.damp * s.state1L;
    sample_t low = s.state2L + c.f_coeff * s.state1L;
    sample_t high = notch - low;
    sample_t band = c.f_coeff * high + s.state1L;
    
    s.state1L = band;
    s.state2L = low;
    
    return in + low * (c.gain_lin - 1.0f);
}

inline sample_t GlobalEQ::processSVF_HighShelf(sample_t in, const SVFCoeffs& c, SVFState& s) {
    sample_t notch = in - c.damp * s.state1L;
    sample_t low = s.state2L + c.f_coeff * s.state1L;
    sample_t high = notch - low;
    sample_t band = c.f_coeff * high + s.state1L;
    
    s.state1L = band;
    s.state2L = low;
    
    return in + high * (c.gain_lin - 1.0f);
}

void __not_in_flash_func(GlobalEQ::processInput)(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
    if (!enabled) return;

    uint8_t activeIdx = activeCoeffsIdx.load(std::memory_order_acquire);

    for (size_t i = 0; i < numSamples; ++i) {
        sample_t inL = left[i];
        sample_t inR = right[i];
        
        for (int b=0; b<3; b++) {
            // Reutilizamos la función temporalmente asumiendo state1L/state2L para el canal que pasemos
            // Para eso, creamos copias de la referencia o la actualizamos. 
            // Espera, processSVF usa state1L y state2L explícitamente. ¡Necesito refactorizarlo!
            // Lo soluciono en línea para el canal L y R.
            
            // Left
            sample_t notchL = inL - coeffs[activeIdx].in[b].damp * inStates[b].state1L;
            sample_t lowL = inStates[b].state2L + coeffs[activeIdx].in[b].f_coeff * inStates[b].state1L;
            sample_t highL = notchL - lowL;
            sample_t bandL = coeffs[activeIdx].in[b].f_coeff * highL + inStates[b].state1L;
            inStates[b].state1L = bandL;
            inStates[b].state2L = lowL;
            inL = inL + bandL * (coeffs[activeIdx].in[b].gain_lin - 1.0f);

            // Right
            sample_t notchR = inR - coeffs[activeIdx].in[b].damp * inStates[b].state1R;
            sample_t lowR = inStates[b].state2R + coeffs[activeIdx].in[b].f_coeff * inStates[b].state1R;
            sample_t highR = notchR - lowR;
            sample_t bandR = coeffs[activeIdx].in[b].f_coeff * highR + inStates[b].state1R;
            inStates[b].state1R = bandR;
            inStates[b].state2R = lowR;
            inR = inR + bandR * (coeffs[activeIdx].in[b].gain_lin - 1.0f);
        }
        
        left[i] = inL;
        right[i] = inR;
    }
}

void __not_in_flash_func(GlobalEQ::processOutput)(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
    if (!enabled) return;

    uint8_t activeIdx = activeCoeffsIdx.load(std::memory_order_acquire);

    for (size_t i = 0; i < numSamples; ++i) {
        sample_t inL = left[i];
        sample_t inR = right[i];
        
        // 0: LowShelf, 1: Peak, 2: HighShelf
        for (int b=0; b<3; b++) {
            sample_t notchL = inL - coeffs[activeIdx].out[b].damp * outStates[b].state1L;
            sample_t lowL = outStates[b].state2L + coeffs[activeIdx].out[b].f_coeff * outStates[b].state1L;
            sample_t highL = notchL - lowL;
            sample_t bandL = coeffs[activeIdx].out[b].f_coeff * highL + outStates[b].state1L;
            outStates[b].state1L = bandL;
            outStates[b].state2L = lowL;

            sample_t notchR = inR - coeffs[activeIdx].out[b].damp * outStates[b].state1R;
            sample_t lowR = outStates[b].state2R + coeffs[activeIdx].out[b].f_coeff * outStates[b].state1R;
            sample_t highR = notchR - lowR;
            sample_t bandR = coeffs[activeIdx].out[b].f_coeff * highR + outStates[b].state1R;
            outStates[b].state1R = bandR;
            outStates[b].state2R = lowR;

            sample_t outLinL, outLinR;
            if (b == 0) { // LowShelf
                outLinL = lowL; outLinR = lowR;
            } else if (b == 1) { // Peak
                outLinL = bandL; outLinR = bandR;
            } else { // HighShelf
                outLinL = highL; outLinR = highR;
            }

            inL = inL + outLinL * (coeffs[activeIdx].out[b].gain_lin - 1.0f);
            inR = inR + outLinR * (coeffs[activeIdx].out[b].gain_lin - 1.0f);
        }
        
        left[i] = inL;
        right[i] = inR;
    }
}
