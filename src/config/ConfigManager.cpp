#include "ConfigManager.h"

ConfigManager::ConfigManager() {
    resetToDefaults();
}

uint16_t ConfigManager::calculateCRC(const GlobalConfig& cfg) {
    const uint8_t* data = (const uint8_t*)&cfg;
    size_t length = sizeof(GlobalConfig) - sizeof(uint16_t);
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xA001;
            else crc >>= 1;
        }
    }
    return crc;
}

void ConfigManager::init() {
    // Inicializar sistema de archivos
    if (!LittleFS.begin()) {
        Serial.println("LittleFS: Fallo al montar. Formateando...");
        LittleFS.format();
        LittleFS.begin();
    }

    // Intentar leer configuración global
    bool validSystem = false;
    if (LittleFS.exists("/system.bin")) {
        File f = LittleFS.open("/system.bin", "r");
        if (f) {
            f.read((uint8_t*)&config, sizeof(GlobalConfig));
            f.close();
            
            uint16_t expectedCrc = calculateCRC(config);
            if (config.magicNumber == CONFIG_MAGIC_NUMBER &&
                config.version == CONFIG_VERSION &&
                config.checksum == expectedCrc) {
                validSystem = true;
            }
        }
    }

    if (!validSystem) {
        Serial.println("LittleFS: System config corrupta/vacía - Creando base de datos...");
        resetToDefaults();
        
        // Crear directorio si no existe
        if (!LittleFS.exists("/presets")) {
            LittleFS.mkdir("/presets");
        }

        // Generar los 120 archivos de presets base (Tardará unos milisegundos)
        for(uint8_t i = 0; i < MAX_PRESETS; i++) {
            resetPresetToDefaults(activePreset);
            snprintf(activePreset.name, sizeof(activePreset.name), "Init B%02dP%d", (i/4)+1, (i%4)+1);
            savePreset(i);
        }
        saveSystem();
        Serial.println("LittleFS: 120 Presets creados exitosamente.");
    } else {
        Serial.println("LittleFS: Carga exitosa de configuración válida.");
        // Validar rangos críticos
        if (config.currentPreset >= MAX_PRESETS) config.currentPreset = 0;
        if (config.exprCalibMax < config.exprCalibMin + 10) {
            config.exprCalibMin = 0; config.exprCalibMax = 3900;
        }
    }

    // Cargar el preset activo actual a la RAM
    loadPreset(config.currentPreset);
}

void ConfigManager::saveSystem() {
    config.checksum = calculateCRC(config);

    // READ-BEFORE-WRITE
    if (LittleFS.exists("/system.bin")) {
        File f = LittleFS.open("/system.bin", "r");
        if (f) {
            static GlobalConfig tempConfig; // Static evita heap allocations y previene Wstack-usage warnings
            if (f.read((uint8_t*)&tempConfig, sizeof(GlobalConfig)) == sizeof(GlobalConfig)) {
                f.close();
                // Si la data es idéntica, cancelar escritura
                if (memcmp(&config, &tempConfig, sizeof(GlobalConfig)) == 0) {
                    return; // Early return para evitar Flash Wear
                }
            } else {
                f.close(); // Fallo de lectura parcial
            }
        }
    }

    File f = LittleFS.open("/system.bin", "w");

    // CORRECCIÓN: Validar que el archivo se abrió correctamente
    if (!f) {
        Serial.println("ERROR CRÍTICO: No se pudo crear /system.bin - Flash llena?");
        return;  // No intentar escribir
    }

    size_t written = f.write((uint8_t*)&config, sizeof(GlobalConfig));
    f.close();

    if (written != sizeof(GlobalConfig)) {
        Serial.println("ERROR: Escritura incompleta en /system.bin");
    }
}

void ConfigManager::loadPreset(uint8_t index) {
    if (index >= MAX_PRESETS) return;

    char path[32];
    snprintf(path, sizeof(path), "/presets/%03d.bin", index);

    if (LittleFS.exists(path)) {
        File f = LittleFS.open(path, "r");

        // CORRECCIÓN: Validar que el archivo se abrió y se leyó correctamente
        if (!f) {
            Serial.printf("ERROR: No se pudo abrir %s - Usando defaults\n", path);
            resetPresetToDefaults(activePreset);
            config.currentPreset = index;
            return;
        }

        size_t bytesRead = f.read((uint8_t*)&activePreset, sizeof(EffectConfig));
        f.close();

        if (bytesRead != sizeof(EffectConfig)) {
            Serial.printf("ERROR: Lectura incompleta de %s (%zu bytes)\n", path, bytesRead);
            resetPresetToDefaults(activePreset);
        }
    } else {
        // Archivo no existe - crear defaults
        resetPresetToDefaults(activePreset);
        snprintf(activePreset.name, sizeof(activePreset.name), "Init B%02dP%d", (index/4)+1, (index%4)+1);
        savePreset(index);  // Crear el archivo
    }

    config.currentPreset = index;
    // IMPORTANTE: Ya NO llamamos a saveSystem() aquí para evitar desgaste
    // prematuro de la Flash al navegar rápidamente con el encoder.
}

void ConfigManager::savePreset(uint8_t index) {
    if (index >= MAX_PRESETS) return;

    if (!LittleFS.exists("/presets")) {
        LittleFS.mkdir("/presets");
    }

    char path[32];
    snprintf(path, sizeof(path), "/presets/%03d.bin", index);

    // READ-BEFORE-WRITE
    if (LittleFS.exists(path)) {
        File f = LittleFS.open(path, "r");
        if (f) {
            static EffectConfig tempConfig; // Static evita heap allocations
            if (f.read((uint8_t*)&tempConfig, sizeof(EffectConfig)) == sizeof(EffectConfig)) {
                f.close();
                if (memcmp(&activePreset, &tempConfig, sizeof(EffectConfig)) == 0) {
                    return; // Early return - preset no ha cambiado en memoria Flash
                }
            } else {
                f.close();
            }
        }
    }

    File f = LittleFS.open(path, "w");

    // CORRECCIÓN CRÍTICA: Validar que el archivo se abrió correctamente
    if (!f) {
        Serial.printf("ERROR CRÍTICO: No se pudo crear %s - Flash llena?\n", path);
        return;  // No intentar escribir
    }

    size_t written = f.write((uint8_t*)&activePreset, sizeof(EffectConfig));
    f.close();

    if (written != sizeof(EffectConfig)) {
        Serial.printf("ERROR: Escritura incompleta en %s (%zu bytes)\n", path, written);
    }
}

void ConfigManager::resetPresetToDefaults(EffectConfig& p) {
    memset(&p, 0, sizeof(EffectConfig));
    snprintf(p.name, sizeof(p.name), "Init Preset");
    p.exprTarget = 0;

    auto initSlot = [](EffectSlotPreset& slot, EffectType type) {
        slot.effectType = type;
        slot.enabled = false;
        slot.paramCount = 0;
        slot.currentChannel = 0;
        slot.sceneIgnore = false;
        memset(slot.params, 0, sizeof(slot.params));
    };

    // Default genérico: Clean Chorus
    initSlot(p.slots[0], EffectType::NOISE_GATE); p.slots[0].enabled = true;
    initSlot(p.slots[1], EffectType::CHORUS);     p.slots[1].enabled = true;
    initSlot(p.slots[2], EffectType::NONE);
    initSlot(p.slots[3], EffectType::REVERB);     p.slots[3].enabled = true;

    // Escenas por defecto
    for(int s=0; s<4; s++) {
        for(int slot=0; slot<MAX_ACTIVE_SLOTS; slot++) {
            p.scenes[s].slotEnabled[slot] = p.slots[slot].enabled;
            p.scenes[s].activeChannel[slot] = 0;
        }
    }
}

void ConfigManager::resetToDefaults() {
    memset(&config, 0, sizeof(GlobalConfig));
    config.magicNumber = CONFIG_MAGIC_NUMBER;
    config.version = CONFIG_VERSION;
    config.exprCalibMin = 0;
    config.exprCalibMax = 3900;
    config.cleanSens = 1.0f;
    config.distSens = 1.0f;
    config.currentPreset = 0;
    config.sceneRevert = false; // Scene Retain
    
    resetPresetToDefaults(activePreset);
}
