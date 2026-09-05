#pragma once

#include "../config/HardwareConfig.h"
#include <atomic>
#include <cmath>
#include <algorithm>
#include <Arduino.h>
#include "DSPTypes.h"
#include "FastMath.h"

struct VitalizerParams {
    sample_t attack;        // 4 bytes
    sample_t sustain;       // 4 bytes
    sample_t sensitivity;   // 4 bytes
    sample_t mix;           // 4 bytes
    sample_t trim_db;       // 4 bytes
    sample_t crossoverFreq; // 4 bytes (80.0f - 150.0f)
    uint8_t smooth_mode; // 1 byte
    uint8_t _padding[7]; // Padding para alcanzar 32 bytes exactos
};
static_assert(sizeof(VitalizerParams) == 32, "Must be exactly 32 bytes for AtomicBridge");

class TransientMaster {
private:
    // Lock-free double buffer para parámetros
    VitalizerParams params_buffer[2];
    std::atomic<int> active_idx{0};
    
    // Envolventes de detector
    sample_t envFast_L{0.0f}, envFast_R{0.0f};
    sample_t envSlow_L{0.0f}, envSlow_R{0.0f};
    
    // Coeficientes HPF Sidechain
    sample_t scZ1_L{0.0f}, scZ1_R{0.0f};
    sample_t scAlpha{0.99f};
    
    // TPDF Dither
    uint32_t ditherSeed{1987654321};
    
    sample_t sampleRate{globalSampleRate};
    sample_t invSampleRate{1.0f / globalSampleRate};
    
    void updateCoefficients(sample_t freq) {
        scAlpha = FastMath::fast_expf(-2.0f * M_PI * freq / sampleRate);
    }

public:
    // Passthrough/Unity Gain compliant (Regla 12)
    TransientMaster() {
        // Inicializar buffers
        params_buffer[0] = {0.5f, 0.0f, 0.3f, 1.0f, 0.0f, 150.0f, 0, {0}};
        params_buffer[1] = params_buffer[0];
        updateCoefficients(150.0f);
    }

    inline void updateDitherSeed(uint32_t slotID) {
        // [PARCHE P0 APLICADO]
        // Se reemplazó el reloj sumativo (micros() + analogRead) que causaría un desbordamiento, 
        // por un XOR matemático, que es seguro y de tiempo constante.
        // También se eliminó el "Magic Number" del pin analógico.
        ditherSeed = analogRead(config::hardware::PIN_ENTROPY_ADC) ^ (uint32_t)micros() ^ (slotID << 8) ^ (ditherSeed << 13) ^ (ditherSeed >> 17) ^ (ditherSeed << 5);
    }

    void init(sample_t sr, uint32_t slotID = 0) {
        // FIX-1 (P0 vptr bug): memset eliminado
        sampleRate = sr;
        invSampleRate = 1.0f / sr;
        updateDitherSeed(slotID);
        updateCoefficients(params_buffer[active_idx.load(std::memory_order_relaxed)].crossoverFreq);
    }

    void setParams(const VitalizerParams& newParams) {
        int current = active_idx.load(std::memory_order_relaxed);
        int next = 1 - current;
        params_buffer[next] = newParams;
        std::atomic_thread_fence(std::memory_order_release);
        active_idx.store(next, std::memory_order_relaxed);
    }

    inline sample_t processSidechainHPF(sample_t sample, sample_t& z1) {
        sample_t out = sample - z1;
        z1 = sample + scAlpha * z1;
        return out;
    }

    inline void processBlock(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
        // Leer parámetros activos
        int current = active_idx.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        const VitalizerParams& p = params_buffer[current];
        
        updateCoefficients(p.crossoverFreq);
        
        sample_t attackCoef = FastMath::fast_expf(-1000.0f * (1.0f / (p.attack * 100.0f + 1.0f)) * invSampleRate);
        sample_t sustainCoef = FastMath::fast_expf(-1000.0f * (1.0f / 200.0f) * invSampleRate); // Fijo por simplicidad
        sample_t trimLinear = FastMath::fast_exp2f(p.trim_db / 6.02f);
        
        for (size_t i = 0; i < numSamples; ++i) {
            // 1. Detección (Mono)
            sample_t envInL = FastMath::f_abs(processSidechainHPF(left[i], scZ1_L));
            sample_t envInR = FastMath::f_abs(processSidechainHPF(right[i], scZ1_R));
            sample_t envIn = std::fmax(envInL, envInR);
            
            envFast_L += (envIn - envFast_L) * (1.0f - attackCoef);
            envSlow_L += (envFast_L - envSlow_L) * (1.0f - sustainCoef);
            
            sample_t diff = envFast_L - envSlow_L;
            sample_t gainMod = 1.0f;
            
            if (diff > 0.0f) {
                gainMod += diff * p.sensitivity * p.attack * 5.0f;
            } else {
                gainMod += diff * p.sensitivity * p.sustain * 5.0f;
            }
            
            // Soft clip interno para seguridad de gainMod
            gainMod = std::fmin(gainMod, 10.0f);
            
            // 2. Aplicación y Trim
            sample_t outL = left[i] * gainMod * trimLinear;
            sample_t outR = right[i] * gainMod * trimLinear;
            
            // 3. Mezcla Dry/Wet
            outL = left[i] + (outL - left[i]) * p.mix;
            outR = right[i] + (outR - right[i]) * p.mix;
            
            // 4. Soft Clipper Global y TPDF Dither
            outL = outL / (1.0f + FastMath::f_abs(outL) * 0.85f);
            outR = outR / (1.0f + FastMath::f_abs(outR) * 0.85f);
            
            ditherSeed ^= (ditherSeed << 13);
            ditherSeed ^= (ditherSeed >> 17);
            ditherSeed ^= (ditherSeed << 5);
            sample_t dither = ((ditherSeed & 0xFFFFFF) / 16777216.0f - 0.5f) / 16777216.0f;
            
            left[i] = outL + dither;
            right[i] = outR + dither;
        }
    }
};
