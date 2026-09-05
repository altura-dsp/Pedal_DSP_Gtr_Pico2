#pragma once

#include <Arduino.h>
#include "hardware/pio.h"
#include "hardware/clocks.h"

/**
 * @brief Constantes de Integrador RFI (Live-Proof)
 *
 * El Integrador RFI requiere N lecturas consecutivas estables para validar
 * un cambio, bloqueando falsos positivos por ruido EMI de escenario.
 */
#define RFI_THRESHOLD 10           // 10 ciclos consecutivos para incrementar
#define RFI_DEBOUNCE_HYSTERESIS 8  // 8 ciclos para detectar (histeresis)

/**
 * @brief Estado de un encoder o footswitch.
 */
struct EncoderState {
    volatile int32_t position;
    volatile int32_t lastPosition;
    uint8_t pinA;
    uint8_t pinB;
    uint8_t pinSW;

    // Para botones (debouncing)
    bool buttonPressed;
    bool shortClick;
    bool longClick;
    bool tunerClick; // [NUEVO] Afinador Rápido (2 segundos)
    unsigned long pressStartTime;
    uint8_t rfiCounter; // Integrador RFI para el switch

    // Integradores RFI para pines rotatorios (A y B)
    uint8_t rfiCounterA;
    uint8_t rfiCounterB;
    uint8_t lastStateA;
};

/**
 * @brief Gestor de LEDs NeoPixel WS2812B (Día 2 - Hardware Base)
 *
 * Controla 4 LEDs WS2812B para indicar estado de presets/bancos.
 * Usa lightWS2812 (bitbang asincrónico) para evitar bloqueos.
 */
struct NeoPixelManager {
    static constexpr uint8_t NEO_COUNT = 4;
    static constexpr uint8_t BRIGHTNESS = 128;  // 50% brillo

    // Colores por banco/preset: {R, G, B}
    static constexpr uint8_t presetColors[4][3] = {
        {0, 255, 0},    // Banco A: Verde
        {255, 0, 0},    // Banco B: Rojo
        {0, 0, 255},    // Banco C: Azul
        {255, 255, 0}   // Banco D: Amarillo
    };

    uint8_t dataBuffer[NEO_COUNT * 3];  // RGB data

    PIO pio = pio0;
    uint sm = 0;

    /**
     * @brief Inicializa los NeoPixels
     */
    void init();

    /**
     * @brief Actualiza los LEDs según banco y preset actuales
     *
     * @param bank Banco actual (0-3)
     * @param preset Preset actual (0-11)
     */
    void update(uint8_t bank, uint8_t preset);
};

/**
 * @brief Gestor de hardware no-bloqueante para UI (Core 1).
 *
 * Lee 3 encoders rotatorios + 4 footswitches + potenciómetros
 * de forma asíncrona usando Integrador RFI para inmunidad EMI.
 */
class HardwareInput {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    HardwareInput();

    void init();

    /**
     * @brief Actualiza la máquina de estados de los botones y el ADC.
     * Llamar en el loop de la UI (Core 1).
     */
    void update();

    EncoderState& getEncoder(uint8_t index) {
        if (index > 2) return encoders[0];
        return encoders[index];
    }

    float getMasterVolume() const { return masterVolume; }

    /**
     * @brief Obtiene el valor suavizado del pedal de expresión.
     * Si no hay pedal conectado, retorna -1.0f (indicando inactivo).
     */
    float getExpressionValue() const { return expressionValue; }

    // Para calibración en UI
    int getRawExpression() const { return rawExpression; }

    void setExpressionCalibration(int minVal, int maxVal) {
        exprMin = minVal;
        exprMax = maxVal;
    }

    /**
     * @brief Función reutilizable de Integrador RFI para cualquier pin digital
     *
     * @param pin Pin GPIO a leer
     * @param counter Referencia al contador RFI (persistente entre llamadas)
     * @return true si el pin está estable en LOW (presionado), false si HIGH
     *
     * @note Requiere 10 ciclos consecutivos LOW para activar, 8 ciclos consecutivos
     *       HIGH para desactivar (histeresis antiflicker).
     */
    static inline bool readButtonWithRFI(uint8_t pin, uint8_t& counter) {
        if (digitalRead(pin) == LOW) {
            // LOW = presionado (INPUT_PULLUP invertido)
            if (counter < RFI_THRESHOLD) counter++;
        } else {
            // HIGH = reposo
            if (counter > 0) counter--;
        }
        return (counter >= RFI_DEBOUNCE_HYSTERESIS);
    }

    /**
     * @brief Obtiene estado de un footswitch específico (1-4)
     */
    bool getFootswitch(uint8_t fsIndex) const {
        if (fsIndex > 3) return false;
        return footswitchStates[fsIndex];
    }

    /**
     * @brief Verifica si se activa Bank Down (FS1 + FS2 presionados >200ms)
     */
    bool isBankDownTriggered() const { return bankDownTriggered; }
    void resetBankDownTrigger() { bankDownTriggered = false; }

    /**
     * @brief Verifica si el throttling térmico está activo
     *
     * @return true si temperatura > 80°C (UI debería reducir a 5Hz)
     */
    bool isThermalThrottleActive() const { return thermalThrottleActive; }

    /**
     * @brief Actualiza el estado de throttling térmico
     *
     * Debe llamarse regularmente desde loop1() para monitorear temperatura.
     * Si temperatura > 80°C, activa throttling. Si < 75°C, desactiva.
     */
    void updateThermalStatus();

    /**
     * @brief Actualiza los LEDs NeoPixel según banco y preset
     *
     * @param bank Banco actual (0-3)
     * @param preset Preset actual (0-11)
     */
    void updateNeoPixels(uint8_t bank, uint8_t preset);

    // ========== DÍA 5 - TAP TEMPO ==========
    /**
     * @brief Obtiene el BPM calculado desde Tap Tempo
     * @return BPM actual (retorna 0 si no hay taps registrados)
     */
    float getTapTempoBPM() const { return tapTempoBPM; }

    /**
     * @brief Obtiene la división métrica actual
     * @return 0=1/4, 1=1/8, 2=1/4 puntillo
     */
    uint8_t getTapDivision() const { return tapDivision; }

    /**
     * @brief Verifica si hubo un nuevo Tap Tempo detectado
     * @return true si FS4 fue presionado (one-shot)
     */
    bool isTapTempoTriggered() const { return tapTempoTriggered; }
    void resetTapTempoTriggered() { tapTempoTriggered = false; }

    /**
     * @brief Verifica si la división cambió (long press FS4)
     * @return true si la división rotó (one-shot)
     */
    bool isDivisionChanged() const { return divisionChanged; }
    void resetDivisionChanged() { divisionChanged = false; }

private:
    EncoderState encoders[3];
    float masterVolume;
    float expressionValue;
    float exprRawFiltered;
    int rawExpression;

    int exprMin = 0;
    int exprMax = 3900;

    unsigned long lastAdcTime;

    // ISR estáticas
    static void isrEnc1A();
    static void isrEnc2A();
    static void isrEnc3A();

    // Puntero global para las ISR
    static HardwareInput* instance;

    // Footswitches FS1-FS4 (Día 2 - Hardware Base)
    static constexpr uint8_t FS_COUNT = 4;
    uint8_t fsPins[FS_COUNT];              // Pines GPIO
    uint8_t fsRfiCounters[FS_COUNT];       // Contadores RFI
    bool footswitchStates[FS_COUNT];       // Estados actuales
    uint32_t fsPressTimes[FS_COUNT];       // Timestamps para Bank Down

    // Bank Down detection
    bool bankDownTriggered;
    uint32_t bankDownHoldTime;

    // Throttling Térmico (Día 1 - Infraestructura Crítica)
    bool thermalThrottleActive;
    uint32_t lastThermalCheck;
    static constexpr uint32_t THERMAL_CHECK_INTERVAL = 1000;  // 1 segundo
    static constexpr float THROTTLE_TEMP_HIGH = 85.0f;        // D3: Activar throttling UI a 85°C (alineado a Touring Grade)
    static constexpr float THROTTLE_TEMP_LOW = 80.0f;         // D3: Desactivar a 80°C (histéresis 5°C)

    // NeoPixels (Día 2 - Hardware Base)
    NeoPixelManager neoPixels;

    // Tap Tempo (Día 5 - Pulido Profesional)
    static constexpr uint16_t TAP_TIMEOUT_MS = 2000;     // 2 segundos timeout
    static constexpr uint16_t TAP_LONGPRESS_MS = 1000;   // 1 segundo para rotar división
    static constexpr uint8_t TAP_HISTORY_SIZE = 4;      // Últimos 4 taps para promedio

    uint32_t tapTimestamps[TAP_HISTORY_SIZE];  // Timestamps de los últimos taps
    uint8_t tapIndex;                          // Índice circular
    uint8_t tapCount;                          // Cantidad de taps registrados
    float tapTempoBPM;                         // BPM calculado
    uint8_t tapDivision;                       // 0=1/4, 1=1/8, 2=1/4 puntillo
    bool tapTempoTriggered;                    // One-shot: nuevo tap detectado
    bool divisionChanged;                      // One-shot: división rotó
    uint32_t fs4PressTime;                     // Timestamp para detectar long press

    void handleEncoderInterrupt(uint8_t index);
    void updateEncodersRFI();
    void updateButtons();
    void updateFootswitches();
    void updateADC();
    void updateTapTempo();                     // Día 5 - Procesa Tap Tempo
};
