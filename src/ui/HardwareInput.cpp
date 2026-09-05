#include "HardwareInput.h"
#include "hardware/gpio.h"
#include "../system/FaultLatch.h"

HardwareInput* HardwareInput::instance = nullptr;

HardwareInput::HardwareInput() : masterVolume(1.0f), expressionValue(-1.0f), exprRawFiltered(0.0f), rawExpression(0), lastAdcTime(0), bankDownTriggered(false), bankDownHoldTime(0), thermalThrottleActive(false), lastThermalCheck(0),
    // Día 5 - Tap Tempo inicialización
    tapIndex(0), tapCount(0), tapTempoBPM(120.0f), tapDivision(0), tapTempoTriggered(false), divisionChanged(false), fs4PressTime(0) {
    instance = this;

    // Inicializar array de timestamps Tap Tempo
    for (uint8_t i = 0; i < TAP_HISTORY_SIZE; i++) {
        tapTimestamps[i] = 0;
    }

    // Configuración inicial de pines - Encoders
    encoders[0].pinA = PIN_ENC1_A; encoders[0].pinB = PIN_ENC1_B; encoders[0].pinSW = PIN_ENC1_SW;
    encoders[1].pinA = PIN_ENC2_A; encoders[1].pinB = PIN_ENC2_B; encoders[1].pinSW = PIN_ENC2_SW;
    encoders[2].pinA = PIN_ENC3_A; encoders[2].pinB = PIN_ENC3_B; encoders[2].pinSW = PIN_ENC3_SW;

    for (int i = 0; i < 3; i++) {
        encoders[i].position = 0;
        encoders[i].lastPosition = 0;
        encoders[i].buttonPressed = false;
        encoders[i].shortClick = false;
        encoders[i].longClick = false;
        encoders[i].tunerClick = false;
        encoders[i].pressStartTime = 0;
        encoders[i].rfiCounter = 0;
        encoders[i].rfiCounterA = 0;
        encoders[i].rfiCounterB = 0;
        encoders[i].lastStateA = HIGH;
    }

    // Configuración inicial de pines - Footswitches (Día 2)
    fsPins[0] = 11;  // FS1
    fsPins[1] = 12;  // FS2
    fsPins[2] = 13;  // FS3
    fsPins[3] = 22;  // FS4

    for (int i = 0; i < FS_COUNT; i++) {
        fsRfiCounters[i] = 0;
        footswitchStates[i] = false;
        fsPressTimes[i] = 0;
    }
}

void HardwareInput::init() {
    // Inicializar Encoders
    for (int i = 0; i < 3; i++) {
        pinMode(encoders[i].pinA, INPUT_PULLUP);
        pinMode(encoders[i].pinB, INPUT_PULLUP);
        pinMode(encoders[i].pinSW, INPUT_PULLUP);
    }

    // Inicializar Footswitches FS1-FS4 (Día 2)
    for (int i = 0; i < FS_COUNT; i++) {
        pinMode(fsPins[i], INPUT_PULLUP);
    }

    pinMode(PIN_MASTER_VOL, INPUT); // ADC0
    pinMode(PIN_EXPRESSION, INPUT); // ADC1

    // Inicializar NeoPixels (Día 2)
    neoPixels.init();

    // Ya no usamos attachInterrupt porque usaremos el Integrador RFI por Polling
}

void HardwareInput::isrEnc1A() {}
void HardwareInput::isrEnc2A() {}
void HardwareInput::isrEnc3A() {}

void HardwareInput::handleEncoderInterrupt(uint8_t index) {
    // Obsoleto, reemplazado por updateEncodersRFI()
}

void HardwareInput::updateEncodersRFI() {
    // Patrón EMI Protection + RFI Integrator (CLAUDE.md)
    for (int i = 0; i < 3; i++) {
        // Muestrear Pin A
        if (digitalRead(encoders[i].pinA) == HIGH) {
            if (encoders[i].rfiCounterA < 255) encoders[i].rfiCounterA++;
        } else {
            if (encoders[i].rfiCounterA > 0) encoders[i].rfiCounterA--;
        }
        
        // Muestrear Pin B
        if (digitalRead(encoders[i].pinB) == HIGH) {
            if (encoders[i].rfiCounterB < 255) encoders[i].rfiCounterB++;
        } else {
            if (encoders[i].rfiCounterB > 0) encoders[i].rfiCounterB--;
        }

        // Schmitt trigger por software (Umbrales 127)
        uint8_t currentStateA = (encoders[i].rfiCounterA > 127) ? HIGH : LOW;
        uint8_t currentStateB = (encoders[i].rfiCounterB > 127) ? HIGH : LOW;

        // Detección de flanco de bajada en A
        if (encoders[i].lastStateA == HIGH && currentStateA == LOW) {
            if (currentStateB == LOW) {
                encoders[i].position++;
            } else {
                encoders[i].position--;
            }
        }
        encoders[i].lastStateA = currentStateA;
    }
}

void HardwareInput::update() {
    updateEncodersRFI();
    updateButtons();
    updateFootswitches();  // Día 2 - Bank Down logic
    updateTapTempo();      // Día 5 - Tap Tempo FS4
    updateThermalStatus();  // Día 1 - Throttling Térmico
    updateADC();
}

void HardwareInput::updateButtons() {
    unsigned long now = millis();

    for (int i = 0; i < 3; i++) {
        // Integrador RFI: inmune a picos EMI (usando constantes documentadas)
        if (digitalRead(encoders[i].pinSW) == LOW) { // LOW = presionado
            if (encoders[i].rfiCounter < RFI_THRESHOLD) encoders[i].rfiCounter++;
        } else {
            if (encoders[i].rfiCounter > 0) encoders[i].rfiCounter--;
        }

        bool isPressed = (encoders[i].rfiCounter >= RFI_DEBOUNCE_HYSTERESIS); // Umbral RFI robusto
        
        // Reseteamos banderas de eventos (one-shot)
        encoders[i].shortClick = false;
        encoders[i].longClick = false;
        encoders[i].tunerClick = false;
        
        if (isPressed && !encoders[i].buttonPressed) {
            // Flanco de bajada (recién presionado)
            encoders[i].buttonPressed = true;
            encoders[i].pressStartTime = now;
        } 
        else if (isPressed && encoders[i].buttonPressed) {
            // Mientras está presionado: Verificar umbral de Tuner (2 segundos)
            unsigned long pressDuration = now - encoders[i].pressStartTime;
            if (pressDuration >= 2000) {
                // Si llegamos a los 2s, disparamos el tunerClick inmediatamente
                encoders[i].tunerClick = true;
                // Reseteamos el start time para no dispararlo continuamente ni atrapar el release
                encoders[i].pressStartTime = now + 999999; 
            }
        }
        else if (!isPressed && encoders[i].buttonPressed) {
            // Flanco de subida (soltado)
            encoders[i].buttonPressed = false;
            
            // Si pressStartTime es enorme, significa que consumimos la acción como TunerClick
            if (encoders[i].pressStartTime < now + 900000) {
                unsigned long pressDuration = now - encoders[i].pressStartTime;
                
                // Debounce > 50ms para evitar ruido
                if (pressDuration > 50) {
                    if (pressDuration >= 500) {
                        encoders[i].longClick = true; // Mantener medio segundo
                    } else {
                        encoders[i].shortClick = true; // Clic normal
                    }
                }
            }
        }
    }
}

void HardwareInput::updateFootswitches() {
    // Día 2 - Lectura de Footswitches FS1-FS4 usando Integrador RFI
    uint32_t now = millis();

    for (int i = 0; i < FS_COUNT; i++) {
        // Usar función readButtonWithRFI con contador persistente
        footswitchStates[i] = readButtonWithRFI(fsPins[i], fsRfiCounters[i]);

        // Track timestamps para Bank Down (FS1 + FS2)
        if (footswitchStates[i]) {
            if (fsPressTimes[i] == 0) {
                fsPressTimes[i] = now;
            }
        } else {
            fsPressTimes[i] = 0;
        }
    }

    // Bank Down logic: FS1 + FS2 presionados simultáneamente >200ms
    if (footswitchStates[0] && footswitchStates[1]) {  // FS1 + FS2
        if (fsPressTimes[0] > 0 && fsPressTimes[1] > 0) {
            uint32_t holdTime = now - max(fsPressTimes[0], fsPressTimes[1]);

            if (holdTime > 200 && !bankDownTriggered) {
                bankDownTriggered = true;
                // Reset timestamps para evitar múltiples triggers
                fsPressTimes[0] = 0;
                fsPressTimes[1] = 0;
            }
        }
    }
}

void HardwareInput::updateThermalStatus() {
    // Día 1 - Throttling Térmico
    uint32_t now = millis();

    // Solo verificar cada 1 segundo (no requiere polling continuo)
    if (now - lastThermalCheck < THERMAL_CHECK_INTERVAL) {
        return;
    }

    lastThermalCheck = now;

    // analogReadTemp() devuelve temperatura en °C (RP2350)
    float temp = analogReadTemp();

    if (temp > THROTTLE_TEMP_HIGH) {
        // Activar throttling si > 85°C (D3 Touring Grade)
        thermalThrottleActive = true;
        globalFaults.registerFault(FaultLatch::THERMAL_THROTTLE);
    } else if (temp < THROTTLE_TEMP_LOW) {
        // Desactivar throttling si < 80°C (histeresis 5°C)
        thermalThrottleActive = false;
    }
    // Entre 80°C y 85°C: mantener estado actual (evita flicker)
}

void HardwareInput::updateADC() {
    unsigned long now = millis();
    // Cachear ADC cada 20ms para el pedal de expresión (mayor responsividad)
    if (now - lastAdcTime >= 20) {
        lastAdcTime = now;
        
        // 1. Lectura del Master Volume con MUX Settle
        analogRead(PIN_MASTER_VOL); // Dummy read para asentar el MUX
        int rawVol = analogRead(PIN_MASTER_VOL); 
        
        // Knob Hysteresis: ignorar ruido ADC (Threshold ~5 LSB)
        static int lastRawVol = -100;
        if (abs(rawVol - lastRawVol) > 5) {
            lastRawVol = rawVol;
            masterVolume = (float)rawVol / 4095.0f;
            if (masterVolume > 1.0f) masterVolume = 1.0f;
        }
        
        // 2. Lectura y Filtrado del Pedal de Expresión (Integrador IIR RFI)
        analogRead(PIN_EXPRESSION); // Dummy read para asentar el MUX
        int rawExpr = analogRead(PIN_EXPRESSION);
        rawExpression = rawExpr; // Para la UI
        
        // Fail-Safe: Si lee > 4000 (abierto), significa que no hay pedal conectado.
        if (rawExpr > 4000) {
            expressionValue = -1.0f; // Bandera de inactivo
        } else {
            // Suavizado Low-Pass para eliminar ruido ADC mecánico
            exprRawFiltered = (exprRawFiltered * 0.8f) + ((float)rawExpr * 0.2f);
            
            // Rango dinámico calibrado
            float range = (float)(exprMax - exprMin);
            if (range < 10.0f) range = 10.0f; // Prevenir división por cero
            
            float val = (exprRawFiltered - (float)exprMin) / range;
            
            if (val > 1.0f) val = 1.0f;
            if (val < 0.0f) val = 0.0f;
            expressionValue = val;
        }
    }
}

// ============================================================================
// Día 2 - NeoPixels Implementation (NATIVE PIO HARDWARE)
// ============================================================================

// WS2812 PIO program (800 kHz) precompilado
static const uint16_t ws2812_program_instructions[] = {
    0x6221, //  0: out    x, 1            side 0 [2] 
    0x1123, //  1: jmp    !x, 3           side 1 [1] 
    0x1400, //  2: jmp    0               side 1 [4] 
    0xa442, //  3: nop                    side 0 [4] 
};

static const pio_program_t ws2812_program = {
    .instructions = ws2812_program_instructions,
    .length = 4,
    .origin = -1,
};

static inline void ws2812_program_init(PIO pio, uint sm, uint offset, uint pin, float freq) {
    pio_gpio_init(pio, pin);
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset, offset + ws2812_program.length - 1);
    
    // Config sideset (1 bit)
    sm_config_set_sideset(&c, 1, false, false);
    sm_config_set_sideset_pins(&c, pin);
    
    // Configurar OSR: Shift out (hacia la derecha false -> MSB), auto pull=true, 24 bits
    sm_config_set_out_shift(&c, false, true, 24);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    // Ciclos por bit = 10 (2 + 1 + 4 + 4 - aprox 10 instrucciones sumando delays)
    int cycles_per_bit = 10;
    float div = clock_get_hz(clk_sys) / (freq * cycles_per_bit);
    sm_config_set_clkdiv(&c, div);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}

void NeoPixelManager::init() {
    // Inicializar buffer a apagado
    for (int i = 0; i < NEO_COUNT * 3; i++) {
        dataBuffer[i] = 0;
    }

    // Inicializar PIO (Hardware)
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, PIN_NEOPIXEL, 800000.0f); // 800 kHz
}

void NeoPixelManager::update(uint8_t bank, uint8_t preset) {
    // Calcular qué presets están en el banco actual
    // Banco 0: Presets 0-3, Banco 1: Presets 4-7, Banco 2: Presets 8-11
    uint8_t bankStartPreset = bank * 4;

    for (int i = 0; i < NEO_COUNT; i++) {
        uint8_t presetIndex = bankStartPreset + i;
        bool isActive = (preset == presetIndex);

        if (isActive) {
            // Preset activo: Color del banco al brillo completo
            dataBuffer[i * 3 + 0] = presetColors[bank][0]; // R
            dataBuffer[i * 3 + 1] = presetColors[bank][1]; // G
            dataBuffer[i * 3 + 2] = presetColors[bank][2]; // B
        } else {
            // Preset inactivo: Apagado
            dataBuffer[i * 3 + 0] = 0;
            dataBuffer[i * 3 + 1] = 0;
            dataBuffer[i * 3 + 2] = 0;
        }
    }

    // NATIVE PIO IMPLEMENTATION
    // CERO bloqueos de interrupción, latencia de audio intacta (0.0 ms)
    for (int i = 0; i < NEO_COUNT; i++) {
        uint8_t r = dataBuffer[i * 3 + 0];
        uint8_t g = dataBuffer[i * 3 + 1];
        uint8_t b = dataBuffer[i * 3 + 2];
        
        // Aplicar master brightness
        r = (r * BRIGHTNESS) >> 8;
        g = (g * BRIGHTNESS) >> 8;
        b = (b * BRIGHTNESS) >> 8;

        // Formato GRB de 24 bits esperado por WS2812
        uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)b;
        
        // Escribir a la FIFO de hardware del PIO (con offset para OSR)
        pio_sm_put_blocking(pio, sm, grb << 8u);
    }
}

void HardwareInput::updateNeoPixels(uint8_t bank, uint8_t preset) {
    neoPixels.update(bank, preset);
}

void HardwareInput::updateTapTempo() {
    uint32_t now = millis();
    
    // Timeout de 2 segundos para resetear el contador de taps
    uint8_t lastIndex = (tapIndex == 0) ? (TAP_HISTORY_SIZE - 1) : (tapIndex - 1);
    if (tapCount > 0 && (now - tapTimestamps[lastIndex]) > TAP_TIMEOUT_MS) {
        tapCount = 0;
        tapIndex = 0;
    }

    static bool lastFs4State = false;
    bool currentFs4State = footswitchStates[3];

    // Flanco de bajada en FS4 (Tap detectado) - INPUT_PULLUP: LOW=presionado
    if (!currentFs4State && lastFs4State) {
        fs4PressTime = now;
        tapTimestamps[tapIndex] = now;
        tapIndex = (tapIndex + 1) % TAP_HISTORY_SIZE;
        
        if (tapCount < TAP_HISTORY_SIZE) {
            tapCount++;
        }

        if (tapCount >= 2) {
            uint32_t sumIntervals = 0;
            uint8_t intervals = tapCount - 1;
            
            for (uint8_t i = 0; i < intervals; i++) {
                uint8_t currIdx = (tapIndex - 1 - i + TAP_HISTORY_SIZE) % TAP_HISTORY_SIZE;
                uint8_t prevIdx = (currIdx - 1 + TAP_HISTORY_SIZE) % TAP_HISTORY_SIZE;
                sumIntervals += (tapTimestamps[currIdx] - tapTimestamps[prevIdx]);
            }
            
            float avgIntervalMs = (float)sumIntervals / intervals;
            if (avgIntervalMs > 0) {
                tapTempoBPM = 60000.0f / avgIntervalMs;
                if (tapTempoBPM < 40.0f) tapTempoBPM = 40.0f;
                if (tapTempoBPM > 300.0f) tapTempoBPM = 300.0f;
                tapTempoTriggered = true;
            }
        }
    }
    
    // Detectar pulsación larga para cambiar divisiones (>1s) - INPUT_PULLUP: LOW=presionado
    if (!currentFs4State && !lastFs4State) {
        if (fs4PressTime > 0 && (now - fs4PressTime) > TAP_LONGPRESS_MS && !divisionChanged) {
            tapDivision = (tapDivision + 1) % 3; // Rota entre 4 divisiones
            divisionChanged = true;
        }
    }
    
    // Reset one-shot de división cuando se suelta FS4 (flanco de subida: false→true)
    if (currentFs4State && !lastFs4State) {
        divisionChanged = false;
        fs4PressTime = 0;
    }

    lastFs4State = currentFs4State;
}
