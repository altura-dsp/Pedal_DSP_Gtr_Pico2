#pragma once
#include "DSPTypes.h"
#include <string.h>

#include "IEffect.h"
#include <atomic>

/**
 * @brief Simulador de Altavoz (Speaker Simulator FPU)
 * 
 * Basado en la respuesta de frecuencia de gabinetes clásicos de guitarra de 12".
 * Utiliza una arquitectura de filtros en cascada y paralelo totalmente flotantes:
 * - HPF (Corte de graves de caja)
 * - 3 BPF en paralelo (Cuerpo, Resonancia de medios, Presencia)
 * - 2 LPF en serie (Corte de brillo y simulación de aire)
 * - Parameter Shadowing
 */
class SpeakerSimEffect final : public IEffect {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    SpeakerSimEffect();

    void init(sample_t sampleRate) override;
    void processBlock(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) override;
    void reset() override;

    void setEnabled(bool state) override { enabled = state; }
    bool isEnabled() const override { return enabled; }

    uint8_t getParamCount() const override;
    ParamInfo getParamInfo(uint8_t index) const override;
    sample_t getParamValue(uint8_t index) const override;
    void setParamValue(uint8_t index, sample_t value) override;

private:
    std::atomic<bool> enabled;
    sample_t sampleRate_;

    // Parameter Shadowing
    std::atomic<sample_t> targetLowCut{100.0f};
    std::atomic<sample_t> targetBody{0.0f};
    std::atomic<sample_t> targetMidScoop{-6.0f};
    std::atomic<sample_t> targetAir{5000.0f};

    struct SpeakerSimCoeffs {
        sample_t hpf_alpha;
        sample_t body_linear;
        sample_t scoop_linear;
        sample_t air_alpha;
    };
    SpeakerSimCoeffs coeffs[2];
    std::atomic<uint8_t> activeCoeffsIdx{0};

    void recalculateCoefficients();

    struct TptSvf { sample_t s1=0.0f; sample_t s2=0.0f; };

    // Coeficientes constantes
    sample_t bpf1_g, bpf1_R, bpf1_h_coef;
    sample_t bpf2_g, bpf2_R, bpf2_h_coef;
    sample_t bpf3_g, bpf3_R, bpf3_h_coef;
    sample_t lpf4_a;

    struct ChannelState {
        sample_t hpf;
        TptSvf svf1;
        TptSvf svf2;
        TptSvf svf3;
        sample_t lpf4, lpf5;
    };

    inline sample_t applyTptSvfBpf(sample_t in, TptSvf& st, sample_t g, sample_t R, sample_t h) {
        sample_t hp = (in - 2.0f * R * st.s1 - st.s2) * h;
        sample_t bp = g * hp + st.s1;
        sample_t lp = g * bp + st.s2;
        st.s1 = g * hp + bp;
        st.s2 = g * bp + lp;
        return bp;
    }
    ChannelState stateL, stateR;

    inline sample_t applyLPF(sample_t x, sample_t& state, sample_t alpha) {
        state += alpha * (x - state);
        return state;
    }

    inline sample_t applyHPF(sample_t x, sample_t& state, sample_t alpha) {
        sample_t low = applyLPF(x, state, alpha);
        return x - low;
    }
};
