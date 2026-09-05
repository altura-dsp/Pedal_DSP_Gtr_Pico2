/**
 * DSPMetrics.h - Sistema de Métricas de Estabilidad DSP
 *
 * RT-Safety Guarantee:
 * - Solo actualiza variables volátiles en ISRs/path de audio
 * - Todo el procesamiento pesado en loop1 (Core 1)
 * - Zero allocations en runtime (sin malloc/String)
 * - < 100 bytes SRAM, < 1KB Flash
 *
 * REV4: Sistema corre ESTÁTICAMENTE a 196.608 MHz
 */

#ifndef DSP_METRICS_H
#define DSP_METRICS_H

#include <Arduino.h>

namespace DSPMetrics {

// ========================================================================
// ESTRUCTURA DE MÉTRICAS (Compacta, Cache-friendly)
// ========================================================================

struct Metrics {
    // Contadores actualizados en ISR (deben ser volátiles)
    volatile uint32_t i2sUnderrunCount;
    volatile uint32_t i2sOverrunCount;

    // Estadísticas de temperatura (actualizadas en loop1)
    int16_t maxTemperature;      // Décimas de °C (ej. 850 = 85.0°C)
    int16_t currentTemperature;  // Décimas de °C
    uint32_t tempWarningCount;   // Cuántas veces excedió 85°C (Touring Grade)

    // Métricas de tiempo
    uint32_t uptimeSeconds;
    uint32_t lastUnderrunTimestamp;

    // Banderas
    bool thermalWarningActive;

    // Constantes — D3: Jerarquía Touring Grade (CLAUDE.md regla 5)
    static constexpr int16_t TEMP_WARNING_THRESHOLD = 850;   // 85.0°C → advertencia + Mute preventivo 100ms
    static constexpr int16_t TEMP_CRITICAL_THRESHOLD = 950;  // 95.0°C → Safe Bypass + Halt (riesgo físico)
    static constexpr uint32_t PRINT_INTERVAL_MS = 5000;      // Reporte cada 5s
};

// Instancia global (acceso controlado)
extern Metrics metrics;

// ========================================================================
// FUNCIONES ISR-SAFE (Solo actualizan volátiles)
// ========================================================================

// Llamar SOLO desde ISR de I2S o callback de audio
inline void recordUnderrun() {
    metrics.i2sUnderrunCount++;
}

inline void recordOverrun() {
    metrics.i2sOverrunCount++;
}

// ========================================================================
// FUNCIONES CORE-1 (Loop1 - Actualización asíncrona)
// ========================================================================

// Inicializar el sistema de métricas
void init();

// Actualizar temperatura y estadísticas (llamar en loop1)
void updateTemperature();

// Verificar advertencias térmicas (llamar en loop1)
bool checkThermalWarning();

// Imprimir reporte por Serial (llamar en loop1, no en ISR)
void printReport();

// Resetear contadores
void reset();

// ========================================================================
// INLINE IMPLEMENTATIONS (Header-only para optimización)
// ========================================================================

inline void init() {
    metrics.i2sUnderrunCount = 0;
    metrics.i2sOverrunCount = 0;
    metrics.maxTemperature = 0;
    metrics.currentTemperature = 0;
    metrics.tempWarningCount = 0;
    metrics.uptimeSeconds = 0;
    metrics.lastUnderrunTimestamp = 0;
    metrics.thermalWarningActive = false;

    // Sensor de temperatura del RP2350 se inicializa automáticamente
}

inline void updateTemperature() {
    // Leer sensor interno del RP2350 usando Arduino API
    // analogReadTemp() devuelve temperatura en grados Celsius como float
    float tempC = analogReadTemp();
    metrics.currentTemperature = static_cast<int16_t>(tempC * 10);

    // Actualizar máximo si supera
    if (metrics.currentTemperature > metrics.maxTemperature) {
        metrics.maxTemperature = metrics.currentTemperature;
    }
}

inline bool checkThermalWarning() {
    bool wasWarning = metrics.thermalWarningActive;
    metrics.thermalWarningActive = (metrics.currentTemperature >= Metrics::TEMP_WARNING_THRESHOLD);

    // Contar transiciones a estado de warning
    if (metrics.thermalWarningActive && !wasWarning) {
        metrics.tempWarningCount++;
    }

    return metrics.thermalWarningActive;
}

// D3: Temperatura crítica (95°C) → Halt (riesgo físico)
inline bool checkThermalCritical() {
    return (metrics.currentTemperature >= Metrics::TEMP_CRITICAL_THRESHOLD);
}

inline void printReport() {
    // Usar snprintf con buffer stack (sin allocations)
    char buffer[160];

    snprintf(buffer, sizeof(buffer),
        "=== DSP Metrics @ 196MHz ===\n"
        "Underruns: %u | Overruns: %u\n"
        "Temp: %u.%u C (Max: %u.%u C)\n"
        "Warnings: %u | Uptime: %us\n",
        metrics.i2sUnderrunCount,
        metrics.i2sOverrunCount,
        metrics.currentTemperature / 10,
        abs(metrics.currentTemperature % 10),
        metrics.maxTemperature / 10,
        abs(metrics.maxTemperature % 10),
        metrics.tempWarningCount,
        metrics.uptimeSeconds
    );

    Serial.println(buffer);
}

inline void reset() {
    // Resetear contadores pero mantener maxTemperature
    uint32_t maxTempSaved = metrics.maxTemperature;
    init();
    metrics.maxTemperature = maxTempSaved;
}

} // namespace DSPMetrics

#endif // DSP_METRICS_H
