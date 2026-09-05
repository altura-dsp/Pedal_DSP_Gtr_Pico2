#pragma once
#include "DSPTypes.h"
#include <string.h>

#include "IEffect.h"
#include <atomic>

#define MAX_DELAY_SAMPLES 19200 // 400ms @ 48kHz

enum class DelayMode {
    STEREO_HIFI,
    MONO_HIFI,
    MONO_LOFI,
    PING_PONG,
    CROSS_FEEDBACK
};

/**
 * @brief Efecto de Delay Analógico Pro FPU
 * 
 * Simula delay analógico BBD usando Float32.
 * Evita buffer overflow en EffectSlots mediante Memory Pooling estático global.
 */
class DelayEffect final : public IEffect {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    DelayEffect();
    ~DelayEffect() override;

    void init(sample_t sampleRate) override;
    void processBlock(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) override;
    void reset() override;

    void setEnabled(bool state) override { enabled.store(state, std::memory_order_relaxed); }
    bool isEnabled() const override { return enabled.load(std::memory_order_relaxed); }

    void setTailOnly(bool state) override { tailsOnly.store(state, std::memory_order_relaxed); }
    bool isTailOnly() const override { return tailsOnly.load(std::memory_order_relaxed); }

    void setDelayMode(DelayMode mode) { currentMode.store(mode, std::memory_order_relaxed); }
    
    // ========== FASE SOLID: Interfaz genérica de parámetros ==========
    uint8_t getParamCount() const override;
    ParamInfo getParamInfo(uint8_t index) const override;
    sample_t getParamValue(uint8_t index) const override;
    void setParamValue(uint8_t index, sample_t value) override;

private:
    std::atomic<bool> enabled;
    std::atomic<bool> tailsOnly;
    std::atomic<DelayMode> currentMode;
    sample_t sampleRate_;

    int poolInstanceId;

    // Punteros al pool estático
    sample_t* bufferL;
    sample_t* bufferR;
    uint32_t writeIndex;

    // Parámetros atómicos UI
    std::atomic<sample_t> targetTimeMs;
    std::atomic<sample_t> targetFeedback;
    std::atomic<sample_t> targetMix;
    std::atomic<sample_t> targetDamping;

    struct DelayCoeffs {
        sample_t timeMs;
        sample_t feedback;
        sample_t mix;
        sample_t dampAlpha;
    };
    DelayCoeffs coeffs[2];
    std::atomic<uint8_t> activeCoeffsIdx{0};

    void recalculateCoefficients();

    // Parámetros suavizados internos
    sample_t currentTimeMs;
    sample_t currentFeedback;
    sample_t currentMix;
    sample_t currentDampingAlpha;

    static constexpr sample_t SMOOTH_COEF = 0.002f;

    // Estados de filtros (Punto Fijo / Float)
    sample_t thiran_z1_L, thiran_z1_R;
    sample_t damp_state_L, damp_state_R;

    sample_t lfoPhase;
    static constexpr sample_t LFO_RATE = 0.5f;

    // Modos de procesamiento
    void processStereoHiFi(sample_t delayInt, sample_t delayFrac, sample_t* __restrict left, sample_t* __restrict right, size_t numSamples);
};
