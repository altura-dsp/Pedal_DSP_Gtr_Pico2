#include "../config/HardwareConfig.h"
#include "GuitarConditionerEffect.h"
#include <math.h>
#include <algorithm>
#include "FastMath.h"

GuitarConditionerEffect::GuitarConditionerEffect() 
    : enabled(true), sampleRate_(globalSampleRate) {
    reset();
}

void GuitarConditionerEffect::init(sample_t sampleRate) {
        // FIX-1 (P0 vptr bug): memset eliminado
    sampleRate_ = sampleRate > 0 ? sampleRate : globalSampleRate;
    reset();

    sample_t overallscale = sampleRate_ / 44.1e3f;

    iirTreble = (0.287496f / overallscale) * 2.0f;
    iirBass = (0.085184f / overallscale) * 2.0f;
    threshTreble = 0.0081f / overallscale;
    threshBass = 0.0256f / overallscale;
}

void GuitarConditionerEffect::reset() {
    iirSampleTAL = iirSampleTBL = 0.0f;
    iirSampleTAR = iirSampleTBR = 0.0f;
    iirSampleBAL = iirSampleBBL = 0.0f;
    iirSampleBAR = iirSampleBBR = 0.0f;
    lastSampleTL = lastSampleTR = 0.0f;
    lastSampleBL = lastSampleBR = 0.0f;
    fpFlip = true;
}

void __not_in_flash_func(GuitarConditionerEffect::processBlock)(sample_t* __restrict left, sample_t* __restrict right, size_t numSamples) {
    if (!enabled) return;

    for (size_t i = 0; i < numSamples; ++i) {
        sample_t inputSampleL = left[i];
        sample_t inputSampleR = right[i];

        // Escudo pasivo unificado contra denormales (FTZ)
        inputSampleL += 1.0e-22f;
        inputSampleR += 1.0e-22f;

        sample_t trebleL = inputSampleL;
        sample_t bassL = inputSampleL;
        sample_t trebleR = inputSampleR;
        sample_t bassR = inputSampleR;
        
        trebleL += trebleL; // +3dB on treble
        trebleR += trebleR;

        sample_t offset, clamp;

        // --- Canal Izquierdo (Treble) ---
        offset = (1.0f + tightTreble) + ((1.0f - FastMath::f_abs(trebleL)) * tightTreble);
        if (offset < 0.0f) offset = 0.0f; else if (offset > 1.0f) offset = 1.0f;
        
        if (fpFlip) {
            iirSampleTAL = (iirSampleTAL * (1.0f - (offset * iirTreble))) + (trebleL * (offset * iirTreble));
            trebleL -= iirSampleTAL;
        } else {
            iirSampleTBL = (iirSampleTBL * (1.0f - (offset * iirTreble))) + (trebleL * (offset * iirTreble));
            trebleL -= iirSampleTBL;
        }

        // --- Canal Derecho (Treble) ---
        offset = (1.0f + tightTreble) + ((1.0f - FastMath::f_abs(trebleR)) * tightTreble);
        if (offset < 0.0f) offset = 0.0f; else if (offset > 1.0f) offset = 1.0f;
        
        if (fpFlip) {
            iirSampleTAR = (iirSampleTAR * (1.0f - (offset * iirTreble))) + (trebleR * (offset * iirTreble));
            trebleR -= iirSampleTAR;
        } else {
            iirSampleTBR = (iirSampleTBR * (1.0f - (offset * iirTreble))) + (trebleR * (offset * iirTreble));
            trebleR -= iirSampleTBR;
        }

        // --- Canal Izquierdo (Bass) ---
        offset = (1.0f - tightBass) + (FastMath::f_abs(bassL) * tightBass);
        if (offset < 0.0f) offset = 0.0f; else if (offset > 1.0f) offset = 1.0f;
        
        if (fpFlip) {
            iirSampleBAL = (iirSampleBAL * (1.0f - (offset * iirBass))) + (bassL * (offset * iirBass));
            bassL -= iirSampleBAL;
        } else {
            iirSampleBBL = (iirSampleBBL * (1.0f - (offset * iirBass))) + (bassL * (offset * iirBass));
            bassL -= iirSampleBBL;
        }

        // --- Canal Derecho (Bass) ---
        offset = (1.0f - tightBass) + (FastMath::f_abs(bassR) * tightBass);
        if (offset < 0.0f) offset = 0.0f; else if (offset > 1.0f) offset = 1.0f;
        
        if (fpFlip) {
            iirSampleBAR = (iirSampleBAR * (1.0f - (offset * iirBass))) + (bassR * (offset * iirBass));
            bassR -= iirSampleBAR;
        } else {
            iirSampleBBR = (iirSampleBBR * (1.0f - (offset * iirBass))) + (bassR * (offset * iirBass));
            bassR -= iirSampleBBR;
        }

        // --- Slew Limiting (Treble L) ---
        inputSampleL = trebleL;
        clamp = inputSampleL - lastSampleTL;
        if (clamp > threshTreble) trebleL = lastSampleTL + threshTreble;
        else if (-clamp > threshTreble) trebleL = lastSampleTL - threshTreble;
        lastSampleTL = trebleL;

        // --- Slew Limiting (Treble R) ---
        inputSampleR = trebleR;
        clamp = inputSampleR - lastSampleTR;
        if (clamp > threshTreble) trebleR = lastSampleTR + threshTreble;
        else if (-clamp > threshTreble) trebleR = lastSampleTR - threshTreble;
        lastSampleTR = trebleR;

        // --- Slew Limiting (Bass L) ---
        inputSampleL = bassL;
        clamp = inputSampleL - lastSampleBL;
        if (clamp > threshBass) bassL = lastSampleBL + threshBass;
        else if (-clamp > threshBass) bassL = lastSampleBL - threshBass;
        lastSampleBL = bassL;

        // --- Slew Limiting (Bass R) ---
        inputSampleR = bassR;
        clamp = inputSampleR - lastSampleBR;
        if (clamp > threshBass) bassR = lastSampleBR + threshBass;
        else if (-clamp > threshBass) bassR = lastSampleBR - threshBass;
        lastSampleBR = bassR;

        // Final merge
        inputSampleL = trebleL + bassL;
        inputSampleR = trebleR + bassR;
        fpFlip = !fpFlip;

        // Clamp final para proteger stages posteriores
        if (inputSampleL > 1.0f) inputSampleL = 1.0f; else if (inputSampleL < -1.0f) inputSampleL = -1.0f;
        if (inputSampleR > 1.0f) inputSampleR = 1.0f; else if (inputSampleR < -1.0f) inputSampleR = -1.0f;

        left[i] = inputSampleL;
        right[i] = inputSampleR;
    }
}

// ========== FASE SOLID: Stub básico ==========

uint8_t GuitarConditionerEffect::getParamCount() const { return 0; }
ParamInfo GuitarConditionerEffect::getParamInfo(uint8_t) const { return {"", 0.0f, 0.0f, "", 0.0f, ParamCurve::LINEAR}; }
sample_t GuitarConditionerEffect::getParamValue(uint8_t) const { return 0.0f; }
void GuitarConditionerEffect::setParamValue(uint8_t, sample_t) { }
