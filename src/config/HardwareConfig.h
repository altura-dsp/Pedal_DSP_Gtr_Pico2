#pragma once

#include <Arduino.h>

/**
 * @file HardwareConfig.h
 * @brief Centralized hardware pin definitions and configuration
 * 
 * Defines all the I/O pins used in the DSP Pedal project to avoid "magic numbers"
 * throughout the codebase.
 */

extern const float globalSampleRate;  // D2: const = alias INMUTABLE de SAMPLE_RATE (cero desincronización posible)

namespace config {
namespace hardware {

    // -------------------------------------------------------------
    // Audio System Rates & I2S (PCM1808 ADC / PCM5102A DAC)
    // -------------------------------------------------------------
    constexpr float SAMPLE_RATE = 48000.0f; // Frecuencia de Muestreo Touring Grade

    // Assuming standard Pico I2S pins based on the project's setup
    // (Actual definitions might be overridden in platformio.ini, but we centralize here)
#ifndef PIN_I2S_BCLK
    constexpr uint8_t PIN_I2S_BCLK = 14;
#endif

#ifndef PIN_I2S_LRCLK
    constexpr uint8_t PIN_I2S_LRCLK = 15;
#endif

#ifndef PIN_I2S_DIN
    constexpr uint8_t PIN_I2S_DIN = 17;
#endif
    
#ifndef PIN_I2S_DOUT
    constexpr uint8_t PIN_I2S_DOUT = 16;
#endif

    // -------------------------------------------------------------
    // True Random Number Generator (TRNG) Entropy Source
    // -------------------------------------------------------------
    // This pin MUST be left floating/unconnected on the PCB.
    // It captures environmental EMF noise to seed the PRNG for dither.
    constexpr uint8_t PIN_ENTROPY_ADC = 29;

    // -------------------------------------------------------------
    // Hardware Safety & Control
    // -------------------------------------------------------------
    constexpr uint8_t PIN_MUTE_RELAY = 23; // Pin de control del relé de mute (Cambiado desde 15/16 para liberar I2S_LRCK)
    constexpr uint8_t ADC_CHANNEL_TEMP = 4; // D5: CANAL ADC interno de temperatura (NO es GPIO físico; GPIO4=ENC1_SW). Naming saneado.
    constexpr uint8_t PIN_BYPASS_CTRL = 24; // D4: Safe Bypass analógico (HEF4053BT en PCB). HIGH=bypass ON (audio entrada→salida, sin DSP).

} // namespace hardware
} // namespace config
