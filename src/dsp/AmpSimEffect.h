// Tube emulation adapted from "Tube2" by Chris Johnson (Airwindows), MIT License.
// Ported and modified for RP2350 DSP. Modifications: GPLv3 (see LICENSE).
#pragma once
#include "DSPTypes.h"
#include <string.h>

#include "IEffect.h"
#include <atomic>
#include <algorithm>

/**
 * @brief Simulador de Amplificador Profesional (FPU / Block Processing)
 * 
 * Simulación de Marshall JCM800 100% en punto flotante nativo:
 * - Emulación asimétrica AIRWINDOWS_TUBE2 a velocidad de hardware real.
 * - Tone Stack en paralelo modelado con RC pasivos.
 * - Parameter Shadowing para evitar glitches en cambios rápidos.
 */
class AmpSimEffect final : public IEffect {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    AmpSimEffect();

    void init(sample_t sampleRate) override;
    void processBlock(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) override;
    void reset() override;

    void setEnabled(bool state) override { enabled = state; }
    bool isEnabled() const override { return enabled; }

    enum class TubeMode { CLASSIC_CUBIC, AIRWINDOWS_TUBE2 };

    uint8_t getParamCount() const override;
    ParamInfo getParamInfo(uint8_t index) const override;
    sample_t getParamValue(uint8_t index) const override;
    void setParamValue(uint8_t index, sample_t value) override;

private:
    std::atomic<bool> enabled;
    sample_t sampleRate_;

    // Parameter Shadowing
    std::atomic<TubeMode> currentTubeMode{TubeMode::CLASSIC_CUBIC};
    std::atomic<sample_t> targetPreGain{0.5f};
    std::atomic<sample_t> targetBass{0.5f};
    std::atomic<sample_t> targetMid{0.5f};
    std::atomic<sample_t> targetTreble{0.5f};
    std::atomic<sample_t> targetPresence{0.5f};
    std::atomic<sample_t> targetMaster{0.5f};

    struct AmpSimCoeffs {
        TubeMode mode;
        sample_t preVol;
        sample_t bassGain;
        sample_t midGain;
        sample_t trebleGain;
        sample_t presenceGain;
        sample_t masterVol;
    };
    AmpSimCoeffs coeffs[2];
    std::atomic<uint8_t> activeCoeffsIdx{0};

    void recalculateCoefficients();

    // Coeficientes de filtros RC
    sample_t jcm_pre_hpf_alpha;
    sample_t jcm_cpl1_alpha;
    sample_t jcm_cpl2_alpha;
    // SmartAmpPro Eq4Band Coeffs
    sample_t a0HI, b1HI;
    sample_t a0MID, b1MID;
    sample_t a0LOW, b1LOW;
    const sample_t cDenorm = 10e-30f;

    struct ChannelState {
        sample_t pre_hpf;
        sample_t cpl1;
        sample_t cpl2;
        // SmartAmpPro Eq4Band States
        sample_t tmplMID;
        sample_t tmplLOW;
        sample_t tmplHI;
    };
    ChannelState stateL, stateR;

    inline sample_t triodeClip(sample_t s, TubeMode mode) {
        if (mode == TubeMode::AIRWINDOWS_TUBE2) {
            // AsymPad constante para emular un bias de distorsión agradable
            const sample_t asymPad = 2.0f; 
            s /= asymPad;
            
            sample_t sharpen = -s;
            if (sharpen > 0.0f) sharpen = 1.0f + sqrtf(sharpen);
            else sharpen = 1.0f - sqrtf(-sharpen);
            
            s -= s * fabsf(s) * sharpen * 0.25f;
            s *= asymPad;
            return std::clamp(s, -1.0f, 1.0f);
        } else {
            // Modo Clásico (Cúbico) con offset (bias)
            sample_t x = s + 0.25f; 
            sample_t out = x - (x * x * x) * 0.33333333f;
            return std::clamp(out, -1.0f, 1.0f);
        }
    }
    
    inline sample_t applyLPF(sample_t x, sample_t& state, sample_t alpha) {
        state += alpha * (x - state);
        return state;
    }
    
    inline sample_t applyHPF(sample_t x, sample_t& state, sample_t alpha) {
        sample_t low = applyLPF(x, state, alpha);
        return x - low;
    }
};
