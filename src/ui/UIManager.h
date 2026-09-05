#pragma once

#include "HardwareInput.h"
#include "DisplayManager.h"
#include "../audio/AudioSystemI2S.h"
#include "config/ConfigManager.h"

enum class MenuState {
    HOME,
    EFFECT_SELECT,
    PARAMETER_EDIT,
    CALIBRATION,
    TUNER,
    GIG_VIEW,
    SAVE_SCREEN
};

/**
 * @brief Orquestador de la Interfaz de Usuario (Core 1).
 * 
 * Conecta los encoders con la pantalla OLED y los efectos DSP.
 */
class UIManager {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    UIManager(AudioSystemI2S& audioRef, ConfigManager& configMgr);
    
    bool init();
    
    /**
     * @brief Actualiza la lógica de UI y repinta la pantalla si es necesario.
     * Limitado internamente a 30fps para no saturar el bus I2C.
     */
    void update();

private:
    HardwareInput input;
    DisplayManager display;
    AudioSystemI2S& audio;
    EffectSlots& slots;
    ConfigManager& configMgr;
    
    MenuState currentState;
    
    unsigned long lastFrameTime;
    
    // Día 1 - Dirty Flags por Zona (Header, Body, Footer)
    // Optimización para OLED I2C: Solo repintar zonas que cambiaron
    struct DirtyFlags {
        bool header : 1;  // Top ~16px (preset name, bank)
        bool body : 1;    // Middle ~32px (parameters, effects)
        bool footer : 1;  // Bottom ~16px (status, volume)

        inline void setAll() { header = body = footer = true; }
        inline void clearAll() { header = body = footer = false; }
        inline bool any() { return header || body || footer; }
    } dirtyFlags;

    
    // Variables de estado del menú
    int8_t selectedEffectIndex;
    int8_t selectedParamIndex;
    
    // Variables de seguimiento para Dirty Flags
    int32_t lastEnc0Pos;
    int32_t lastEnc1Pos;
    int32_t lastEnc2Pos;
    float lastVolume;
    
    void processInputs();
    void renderUI();
    
    // Lógica de menús
    void processHomeMenu();
    void processEffectMenu();
    void processParamMenu();
    void processCalibrationMenu();
    void processGigView();
    void processSaveScreen();
    
    void renderHomeMenu();
    void renderEffectMenu();
    void renderParamMenu();
    void renderCalibrationMenu();
    void renderGigView();
    void renderSaveScreen();
    
    void applyPreset(uint8_t index);
    void saveCurrentToPreset();
    
    // Día 2 - Scenes/Snapshots (Lógica Gapless)
    void loadScene(uint8_t sceneIdx);
    void saveCurrentToScene();
    uint8_t currentScene = 0;
    bool lastFootswitchStates[4] = {false, false, false, false};

    // Día 2 - Actualizar NeoPixels cuando cambia preset
    void updateNeoPixels();

    unsigned long lastActivityTime;
    unsigned long lastPresetChangeTime;
    bool presetNeedsSave;

    // Estado para pedal de expresión (en lugar de static)
    float smoothedExprVal = 0.0f;
    
    // Variables para UI Militar
    bool isScreenDimmed = false;
    bool isScreenSleeping = false;
    uint8_t saveTargetPreset = 0;
};
