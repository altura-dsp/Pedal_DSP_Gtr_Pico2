#include "../config/HardwareConfig.h"
#include "AmpSimEffect.h"
#include "pico.h"
#include "../audio/MathUtils.h"
#include <math.h>

AmpSimEffect::AmpSimEffect() : enabled(false), sampleRate_(globalSampleRate) {
    reset();
}

void AmpSimEffect::init(sample_t sampleRate) {
    sampleRate_ = (sampleRate > 0) ? sampleRate : globalSampleRate;
    
    // Configuración de Voicing Marshall JCM800
    jcm_pre_hpf_alpha = MathUtils::lpf_alpha_float(20.0f / sampleRate_);
    jcm_cpl1_alpha    = MathUtils::lpf_alpha_float(12.0f / sampleRate_);
    jcm_cpl2_alpha    = MathUtils::lpf_alpha_float(40.0f / sampleRate_);
    
    // SmartAmpPro Frequencies
    sample_t srate = sampleRate_;
    sample_t pi = 3.1415926535f;
    
    sample_t xHI = expf(-2.0f * pi * 5000.0f / srate);
    a0HI = 1.0f - xHI;
    b1HI = -xHI;

    sample_t xMID = expf(-2.0f * pi * 2000.0f / srate);
    a0MID = 1.0f - xMID;
    b1MID = -xMID;

    sample_t xLOW = expf(-2.0f * pi * 200.0f / srate);
    a0LOW = 1.0f - xLOW;
    b1LOW = -xLOW;
    
    recalculateCoefficients();
}

void AmpSimEffect::reset() {
    memset(&stateL, 0, sizeof(stateL));
    memset(&stateR, 0, sizeof(stateR));
}

void AmpSimEffect::recalculateCoefficients() {
    uint8_t nextIdx = activeCoeffsIdx.load(std::memory_order_relaxed) ^ 1;
    
    coeffs[nextIdx].mode = currentTubeMode.load(std::memory_order_relaxed);
    
    sample_t preGainParam = targetPreGain.load(std::memory_order_relaxed);
    coeffs[nextIdx].preVol = powf(preGainParam, 1.5f);
    
    // Eq4Band usa exp(slider / 8.65617025)
    // Asumiremos slider de -12 a +12 dB
    auto dbToGain = [](sample_t param0to1) {
        sample_t db = (param0to1 - 0.5f) * 24.0f; // -12 a +12
        return expf(db / 8.65617025f);
    };
    
    coeffs[nextIdx].bassGain = dbToGain(targetBass.load(std::memory_order_relaxed));
    coeffs[nextIdx].midGain = dbToGain(targetMid.load(std::memory_order_relaxed));
    coeffs[nextIdx].trebleGain = dbToGain(targetTreble.load(std::memory_order_relaxed));
    coeffs[nextIdx].presenceGain = dbToGain(targetPresence.load(std::memory_order_relaxed));
    coeffs[nextIdx].masterVol = targetMaster.load(std::memory_order_relaxed);

    activeCoeffsIdx.store(nextIdx, std::memory_order_release);
}

void __not_in_flash_func(AmpSimEffect::processBlock)(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
    if (!enabled.load(std::memory_order_relaxed)) return;

    // 1. Parameter Shadowing
    uint8_t activeIdx = activeCoeffsIdx.load(std::memory_order_acquire);
    TubeMode mode = coeffs[activeIdx].mode;
    sample_t preVol = coeffs[activeIdx].preVol;
    sample_t bassGain = coeffs[activeIdx].bassGain;
    sample_t midGain = coeffs[activeIdx].midGain;
    sample_t trebleGain = coeffs[activeIdx].trebleGain;
    sample_t presenceGain = coeffs[activeIdx].presenceGain;
    sample_t masterVol = coeffs[activeIdx].masterVol;

    auto processChannel = [&](sample_t s, ChannelState& st) {
        // 1. Filtros de entrada y acople
        s = applyHPF(s, st.pre_hpf, jcm_pre_hpf_alpha);
        s = applyHPF(s, st.cpl1, jcm_cpl1_alpha);
        
        // 2. Etapa de Ganancia Pre-Amp (V1A)
        s *= preVol;
        s = triodeClip(s, mode);
        
        // 3. Acople inter-etapa (V2A Cold Clipper)
        s = applyHPF(s, st.cpl2, jcm_cpl2_alpha);
        s = triodeClip(s * 4.0f, mode); // +12dB makeup gain (4.0x)
        
        // 4. SmartAmpPro Tone Stack (Eq4Band Interactive EQ)
        sample_t low0 = (st.tmplMID = a0MID * s - b1MID * st.tmplMID + cDenorm);
        sample_t spl0 = (st.tmplLOW = a0LOW * low0 - b1LOW * st.tmplLOW + cDenorm);
        sample_t lowS0 = low0 - spl0;
        sample_t hi0 = s - low0;
        sample_t midS0 = (st.tmplHI = a0HI * hi0 - b1HI * st.tmplHI + cDenorm);
        sample_t highS0 = hi0 - midS0;
        
        sample_t tone_out = (spl0 * bassGain + lowS0 * midGain + midS0 * trebleGain + highS0 * presenceGain);
        
        // 6. Master Volume
        return tone_out * masterVol;
    };

    for (size_t i = 0; i < numSamples; ++i) {
        left[i] = processChannel(left[i], stateL);
        right[i] = processChannel(right[i], stateR);
    }
}

// ========== FASE SOLID: Implementación de interfaz genérica ==========

uint8_t AmpSimEffect::getParamCount() const { return 6; }

ParamInfo AmpSimEffect::getParamInfo(uint8_t index) const {
    switch(index) {
        case 0: return {"PreGain", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        case 1: return {"Bass", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        case 2: return {"Mid", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        case 3: return {"Treble", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        case 4: return {"Presence", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        case 5: return {"Master", 0.0f, 1.0f, "%.0f %%", 0.05f, ParamCurve::LINEAR};
        default: return {"", 0.0f, 0.0f, "", 0.0f, ParamCurve::LINEAR};
    }
}

sample_t AmpSimEffect::getParamValue(uint8_t index) const {
    switch(index) {
        case 0: return targetPreGain.load();
        case 1: return targetBass.load();
        case 2: return targetMid.load();
        case 3: return targetTreble.load();
        case 4: return targetPresence.load();
        case 5: return targetMaster.load();
        default: return 0.0f;
    }
}

void AmpSimEffect::setParamValue(uint8_t index, sample_t value) {
    switch(index) {
        case 0: targetPreGain.store(value, std::memory_order_relaxed); break;
        case 1: targetBass.store(value, std::memory_order_relaxed); break;
        case 2: targetMid.store(value, std::memory_order_relaxed); break;
        case 3: targetTreble.store(value, std::memory_order_relaxed); break;
        case 4: targetPresence.store(value, std::memory_order_relaxed); break;
        case 5: targetMaster.store(value, std::memory_order_relaxed); break;
    }
    recalculateCoefficients();
}
