#pragma once

#include <Arduino.h>
#include "hardware/watchdog.h"

/**
 * @brief Fault Latch System (Producción - V2)
 * 
 * Registra condiciones de falla persistentes en el Scratch Register 5 del hardware Watchdog.
 * Esto asegura que la memoria de fallos sobreviva a reinicios por Stack Overflow o peralte térmico,
 * sin colisionar con el bootloader de Earle Philhower (que usa Scratch 4).
 */
struct FaultLatch {
    enum FaultType {
        I2S_UNDERRUN = 0,
        CPU_THROTTLE = 1,
        THERMAL_THROTTLE = 2,
        I2C_ERROR = 3,
        STACK_OVERFLOW = 4,
        THERMAL_CRITICAL = 5  // D3: Halt a 95°C (riesgo físico)
    };

    FaultLatch() {}

    void registerFault(FaultType fault) {
        // Establecer el bit correspondiente
        watchdog_hw->scratch[5] |= (1 << fault);
        
        // Incrementar el contador (bits 8-15)
        uint8_t count = (watchdog_hw->scratch[5] >> 8) & 0xFF;
        if (count < 255) {
            count++;
            watchdog_hw->scratch[5] = (watchdog_hw->scratch[5] & 0x000000FF) | (count << 8);
        }
    }

    void reset() {
        watchdog_hw->scratch[5] = 0;
    }

    bool i2sUnderrun() const { return (watchdog_hw->scratch[5] & (1 << I2S_UNDERRUN)) != 0; }
    bool cpuThrottle() const { return (watchdog_hw->scratch[5] & (1 << CPU_THROTTLE)) != 0; }
    bool thermalThrottle() const { return (watchdog_hw->scratch[5] & (1 << THERMAL_THROTTLE)) != 0; }
    bool thermalCritical() const { return (watchdog_hw->scratch[5] & (1 << THERMAL_CRITICAL)) != 0; }
    bool i2cError() const { return (watchdog_hw->scratch[5] & (1 << I2C_ERROR)) != 0; }
    bool stackOverflow() const { return (watchdog_hw->scratch[5] & (1 << STACK_OVERFLOW)) != 0; }
    
    uint8_t faultCount() const {
        return (watchdog_hw->scratch[5] >> 8) & 0xFF;
    }
};

// Instancia global definida en main.cpp
extern FaultLatch globalFaults;
