#include "ZLWarmEffect.h"
#include <algorithm>

ZLWarmEffect::ZLWarmEffect() : enabled(true) {
    curve.store(1.0f);
    warm.store(1.0f);
    wet.store(1.0f);
    recalculateCoefficients();
}

void ZLWarmEffect::init(sample_t sampleRate) {
        // FIX-1 (P0 vptr bug): memset eliminado
    (void)sampleRate;
    recalculateCoefficients();
}

void ZLWarmEffect::reset() {
    // No state to reset
}

void ZLWarmEffect::recalculateCoefficients() {
    uint8_t nextIdx = activeCoeffsIdx.load(std::memory_order_relaxed) ^ 1;
    sample_t c = curve.load(std::memory_order_relaxed);
    
    coeffs[nextIdx].p_a = 0.25f * (c - 1.0f);
    coeffs[nextIdx].p_b = 0.5f * (c - 1.0f);
    coeffs[nextIdx].p_c = 0.75f - (1.75f * c);
    coeffs[nextIdx].p_d = c + 1.0f;
    coeffs[nextIdx].p_comp = 1.0f / (0.5625f * c + 1.125f);

    coeffs[nextIdx].n_a = 1.35f;
    coeffs[nextIdx].n_b = -3.35f + 0.75f * c;
    coeffs[nextIdx].n_c = 1.95f - 1.75f * c;
    coeffs[nextIdx].n_d = c + 1.0f;
    coeffs[nextIdx].n_comp = 1.0f / (0.5625f * c + 1.125f);
    
    coeffs[nextIdx].warm = warm.load(std::memory_order_relaxed);
    coeffs[nextIdx].wet = wet.load(std::memory_order_relaxed);

    activeCoeffsIdx.store(nextIdx, std::memory_order_release);
}

inline sample_t ZLWarmEffect::shape(sample_t x, sample_t w, sample_t pa, sample_t pb, sample_t pc, sample_t pd, sample_t pcomp, sample_t na, sample_t nb, sample_t nc, sample_t nd, sample_t ncomp) const {
    if (x > 0.0f) {
        return pcomp * x * (pd + x * (pc + x * (pb + pa * x)));
    } else {
        sample_t abs_x = -x;
        sample_t p_val = pcomp * abs_x * (pd + abs_x * (pc + abs_x * (pb + pa * abs_x)));
        sample_t n_val = ncomp * abs_x * (nd + abs_x * (nc + abs_x * (nb + na * abs_x)));
        return -w * n_val - (1.0f - w) * p_val;
    }
}

void ZLWarmEffect::processBlock(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
    if (!enabled.load(std::memory_order_relaxed)) return;

    // Parameter Shadowing
    uint8_t activeIdx = activeCoeffsIdx.load(std::memory_order_acquire);
    const sample_t w = coeffs[activeIdx].warm;
    const sample_t wt = coeffs[activeIdx].wet;
    
    const sample_t pa = coeffs[activeIdx].p_a;
    const sample_t pb = coeffs[activeIdx].p_b;
    const sample_t pc = coeffs[activeIdx].p_c;
    const sample_t pd = coeffs[activeIdx].p_d;
    const sample_t pcomp = coeffs[activeIdx].p_comp;
    
    const sample_t na = coeffs[activeIdx].n_a;
    const sample_t nb = coeffs[activeIdx].n_b;
    const sample_t nc = coeffs[activeIdx].n_c;
    const sample_t nd = coeffs[activeIdx].n_d;
    const sample_t ncomp = coeffs[activeIdx].n_comp;

    for (size_t i = 0; i < numSamples; ++i) {
        sample_t inL = std::clamp(left[i], -1.0f, 1.0f);
        sample_t inR = std::clamp(right[i], -1.0f, 1.0f);
        
        sample_t outL = wt * shape(inL, w, pa, pb, pc, pd, pcomp, na, nb, nc, nd, ncomp) + (1.0f - wt) * inL;
        sample_t outR = wt * shape(inR, w, pa, pb, pc, pd, pcomp, na, nb, nc, nd, ncomp) + (1.0f - wt) * inR;
        
        left[i] = std::clamp(outL, -1.0f, 1.0f);
        right[i] = std::clamp(outR, -1.0f, 1.0f);
    }
}

uint8_t ZLWarmEffect::getParamCount() const { return 3; }

ParamInfo ZLWarmEffect::getParamInfo(uint8_t index) const {
    switch (index) {
        case 0: return {"Curve", -1.0f, 1.0f, "%.2f", 0.05f, ParamCurve::LINEAR};
        case 1: return {"Warmth", 0.0f, 100.0f, "%.0f %%", 5.0f, ParamCurve::LINEAR};
        case 2: return {"Wet Mix", 0.0f, 100.0f, "%.0f %%", 5.0f, ParamCurve::LINEAR};
        default: return {"", 0.0f, 0.0f, "", 0.0f, ParamCurve::LINEAR};
    }
}

sample_t ZLWarmEffect::getParamValue(uint8_t index) const {
    switch (index) {
        case 0: return curve.load(std::memory_order_relaxed);
        case 1: return warm.load(std::memory_order_relaxed) * 100.0f;
        case 2: return wet.load(std::memory_order_relaxed) * 100.0f;
        default: return 0.0f;
    }
}

void ZLWarmEffect::setParamValue(uint8_t index, sample_t value) {
    switch (index) {
        case 0: 
            curve.store(std::clamp(value, -1.0f, 1.0f), std::memory_order_relaxed); 
            break;
        case 1: 
            warm.store(std::clamp(value / 100.0f, 0.0f, 1.0f), std::memory_order_relaxed); 
            break;
        case 2: 
            wet.store(std::clamp(value / 100.0f, 0.0f, 1.0f), std::memory_order_relaxed); 
            break;
    }
    recalculateCoefficients();
}
