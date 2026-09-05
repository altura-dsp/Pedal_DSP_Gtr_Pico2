#pragma once
#include "DSPTypes.h"
#include <string.h>

#include <stdint.h>

/**
 * @brief Tablas precalculadas en memoria inmutable (PROGMEM)
 * para optimizar drásticamente la generación de osciladores de baja frecuencia.
 */
namespace LfoTables {
    
    // Tamaño estándar de nuestras tablas LFO
    constexpr uint16_t TABLE_SIZE = 256;

    // Tabla Senoidal en formato Q8.24 (Amplitud = 0x01000000 = 1.0)
    extern const int32_t sineTableQ24[TABLE_SIZE];

}
