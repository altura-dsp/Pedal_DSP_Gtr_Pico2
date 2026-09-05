#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include "../audio/EffectSlots.h"
#include "../audio/ControlMatrix.h"

#define CONFIG_MAGIC_NUMBER 0x717A
#define CONFIG_VERSION 7  // Actualizado para Nombres de Presets
#define MAX_PRESETS 120    // 30 Bancos de 4 presets

// Máximo número de parámetros por efecto (ParametricEQ tiene 18)
#define MAX_PARAMS_PER_EFFECT 18

/**
 * @brief Configuración genérica de un slot de efecto (SOLID v5 + Channels v6)
 */
struct EffectSlotPreset {
    EffectType effectType;
    bool enabled;
    uint8_t paramCount;  // Número real de parámetros del efecto
    float params[4][MAX_PARAMS_PER_EFFECT];  // Parámetros por canal (A, B, C, D)
    uint8_t currentChannel; // Canal actualmente activo para este bloque (0-3)
    bool sceneIgnore;       // Si true, cambios de escena no alteran estado ni canal
};

/**
 * @brief Configuración de slot de ControlMatrix (persistente)
 *
 * Almacena la configuración de mapeo para cada slot.
 */
struct MatrixSlotConfig {
    MatrixSource sourceType;
    uint8_t targetEffect;   // EffectType enum
    uint8_t targetParam;    // ParamIndex
    bool enabled;
};

/**
 * @brief Configuración de Escena (Día 2)
 * Almacena el estado On/Off de los 4 slots.
 */
struct SceneConfig {
    bool slotEnabled[MAX_ACTIVE_SLOTS];
    uint8_t activeChannel[MAX_ACTIVE_SLOTS]; // Canal A/B/C/D (0-3) para este slot en esta escena
};

/**
 * @brief Estructura de preset completa (SOLID v5 + ControlMatrix)
 *
 * Contiene información de slots + configuración global de expresión + ControlMatrix.
 */
struct EffectConfig {
    char name[16];           // Nombre del Preset (e.g. "Clean Chorus")
    EffectSlotPreset slots[MAX_ACTIVE_SLOTS];
    uint8_t exprTarget;  // 0=Wah, 1=Vol, 2=DelayMix, 3=DistGain

    // Día 2 - Scenes/Snapshots (4 escenas por preset)
    SceneConfig scenes[4];

    // Día 3 - ControlMatrix (8 slots de mapeo persistente)
    MatrixSlotConfig matrixSlots[MATRIX_SOURCES];
};

// Estructura global (Persistencia en Flash)
struct GlobalConfig {
    uint16_t magicNumber;
    uint8_t version;
    int exprCalibMin;
    int exprCalibMax;

    // Módulo de Sensibilidad Global (Fase Pro - rev2.md)
    // Ajuste de ganancia de entrada para diferentes tipos de pastillas
    // Single Coil (señal débil) vs Humbucker (señal fuerte)
    float cleanSens;   // Sensibilidad para tonos limpios (-12dB a +12dB, 1.0 = unity)
    float distSens;    // Sensibilidad para distorsión (-12dB a +12dB, 1.0 = unity)

    uint8_t currentPreset;
    bool sceneRevert;  // Día 6 - Modo de cambio de escena (Revert vs Retain)

    // ELIMINADO: EffectConfig presets[MAX_PRESETS]; -> Migrado a LittleFS

    uint16_t checksum; // Debe ir al final
};

/**
 * @brief Gestor de persistencia EEPROM (Cero asignación dinámica, lecturas seguras)
 */
class ConfigManager {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    ConfigManager();

    void init();
    void saveSystem();
    void savePreset(uint8_t index);
    void loadPreset(uint8_t index);

    GlobalConfig& getConfig() { return config; }
    void resetToDefaults();

    // Obtener configuración del preset activo en RAM
    EffectConfig& getCurrentPreset() { return activePreset; }

private:
    uint16_t calculateCRC(const GlobalConfig& cfg);
    GlobalConfig config;
    EffectConfig activePreset;
    void resetPresetToDefaults(EffectConfig& p);
};
