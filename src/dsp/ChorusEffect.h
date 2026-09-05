#pragma once
#include "DSPTypes.h"
#include <string.h>

#include "IEffect.h"
#include <atomic>

#define CHORUS_BUFFER_SIZE 512 // ~10.6ms at 48kHz
#define MAX_CHORUS_INSTANCES 2

/**
 * @brief ChorusEffect FPU (Fase 7)
 * 
 * Chorus estéreo masivo con LFO multi-fásico (3 fases a 120 grados).
 * - Utiliza Buffer Pooling Estático (Patrón DelayEffect) para no 
 *   consumir RAM dinámica del sistema de Slots.
 * - Interpolación Thiran para fractional delay de alta fidelidad.
 */
class ChorusEffect final : public IEffect {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    ChorusEffect();
    ~ChorusEffect() override;

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
    std::atomic<sample_t> targetMix;
    std::atomic<sample_t> targetVolume;

    // ========== Double-Buffering State ==========
    struct ChorusCoeffs {
        sample_t rate;
        sample_t depth;
        sample_t mix;
        sample_t volume;
    };
    
    // Estado de filtros BBD
    sample_t bbd_lpf_L = 0.0f;
    sample_t bbd_lpf_R = 0.0f;
    
    ChorusCoeffs coeffs[2];
    std::atomic<uint8_t> activeCoeffsIdx{0};

    void recalculateCoefficients();
    
    sample_t lfoPhase;
    uint32_t writeIndex;
    uint32_t rngState = 0x12345678;

    inline uint32_t xorshift32() {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return rngState;
    }
    // Estados Thiran (4 cabezales de lectura para 3 fases estéreo)
    sample_t thiranL1;
    sample_t thiranL2;
    sample_t thiranR2;
    sample_t thiranR3;

    // Buffer Pooling Externo (Zero RAM Hog)
    int poolInstanceId;
    sample_t* bufferL;
    sample_t* bufferR;

    static constexpr sample_t SMOOTH_COEF = 0.005f;
    
    // Función inline para lectura interpolada con Thiran 1er orden
    inline sample_t readDelay(sample_t* buffer, sample_t delaySamples, sample_t& thiranState) {
        uint32_t delayInt = static_cast<uint32_t>(delaySamples);
        sample_t delayFrac = delaySamples - (sample_t)delayInt;

        // FIX (P2): Máscara de bits para buffers en potencia de 2
        uint32_t readIdx = (CHORUS_BUFFER_SIZE + writeIndex - delayInt) & (CHORUS_BUFFER_SIZE - 1);
        uint32_t readIdxNext = (readIdx + 1) & (CHORUS_BUFFER_SIZE - 1);

        sample_t v0 = buffer[readIdx];
        sample_t v1 = buffer[readIdxNext];
        sample_t linearOut = v0 + delayFrac * (v1 - v0);
        
        sample_t thiranCoef = (1.0f - delayFrac) / (1.0f + delayFrac);
        sample_t thiranOut = thiranCoef * (linearOut - thiranState) + v0;
        thiranState = linearOut;
        
        return thiranOut;
    }
};
