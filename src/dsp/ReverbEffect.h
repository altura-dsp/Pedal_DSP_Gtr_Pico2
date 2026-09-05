#pragma once
#include "DSPTypes.h"
#include <string.h>

#include "IEffect.h"
#include <atomic>

#define FDN_DL1 1103
#define FDN_DL2 1217
#define FDN_DL3 1399
#define FDN_DL4 1543

/**
 * @brief Reverb Profesional FDN (Feedback Delay Network) FPU
 * 
 * Implementación 100% Float32.
 * Evita overflow de la RAM usando Pooling estático global de buffers.
 */
class ReverbEffect final : public IEffect {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    ReverbEffect();
    ~ReverbEffect() override;

    void init(sample_t sampleRate) override;
    void processBlock(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) override;
    void reset() override;

    void setEnabled(bool state) override { enabled.store(state, std::memory_order_relaxed); }
    bool isEnabled() const override { return enabled.load(std::memory_order_relaxed); }

    void setTailOnly(bool state) override { tailsOnly.store(state, std::memory_order_relaxed); }
    bool isTailOnly() const override { return tailsOnly.load(std::memory_order_relaxed); }

    // ========== FASE SOLID: Interfaz genérica de parámetros ==========
    uint8_t getParamCount() const override;
    ParamInfo getParamInfo(uint8_t index) const override;
    sample_t getParamValue(uint8_t index) const override;
    void setParamValue(uint8_t index, sample_t value) override;

private:
    std::atomic<bool> enabled;
    std::atomic<bool> tailsOnly;
    sample_t sampleRate_;
    int poolInstanceId;

    // Parámetros atómicos
    std::atomic<sample_t> targetSize;
    std::atomic<sample_t> targetDamp;
    std::atomic<sample_t> targetMix;

    // ========== Double-Buffering State ==========
    struct ReverbCoeffs {
        sample_t size;
        sample_t mix;
        sample_t filter_alpha;
    };
    
    ReverbCoeffs coeffs[2];
    std::atomic<uint8_t> activeCoeffsIdx{0};

    void recalculateCoefficients();

    // Punteros al pool estático
    sample_t* dl1;
    sample_t* dl2;
    sample_t* dl3;
    sample_t* dl4;

    uint32_t ptr1, ptr2, ptr3, ptr4;
    sample_t damp_s1, damp_s2, damp_s3, damp_s4;
};
