#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1 // No reset pin
#define SCREEN_ADDRESS 0x3C

/**
 * @brief Gestor de pantalla OLED SSD1306 (Core 1).
 * 
 * Utiliza el patrón "Dirty Flag" para repintar solo secciones de la pantalla
 * que hayan cambiado, minimizando la carga en el bus I2C.
 */
class DisplayManager {
public:
    // Passthrough/Unity Gain compliant (Regla 12)
    DisplayManager();
    
    bool init();
    
    // Control de Fósforo y Energía
    void setContrast(uint8_t c);
    void sleep();
    void wake();
    
    /**
     * @brief Dibuja el marco y textos estáticos de la interfaz actual.
     * Solo debe llamarse al cambiar de menú, NUNCA en el bucle principal.
     */
    void drawStaticUI(const char* title);
    
    /**
     * @brief Actualiza un valor en pantalla sobrescribiendo el área con fondo negro.
     * 
     * @param label Etiqueta corta (ej. "Gain").
     * @param value Valor numérico o texto a mostrar.
     * @param line Índice de la línea (0 a 3).
     * @param selected True si este parámetro está seleccionado (se dibuja invertido).
     */
    void updateParameterLine(const char* label, const char* value, uint8_t line, bool selected);
    
    /**
     * @brief Dibuja la interfaz del afinador.
     * @param note Nombre de la nota (ej. "A#").
     * @param cents Desviación en cents (-50 a 50).
     * @param signal True si hay señal válida.
     */
    void drawTuner(const char* note, int8_t cents, bool signal);

    /**
     * @brief Dibuja la pantalla interactiva de guardado
     */
    void drawSaveScreen(uint8_t targetPreset);

    /**
     * @brief Dibuja barras de interfaz militar
     */
    void drawBipolarBar(uint8_t y, const char* label, float value, float origValue, float minVal, float maxVal, const char* unit);
    void drawUnipolarBar(uint8_t y, const char* label, float value, float origValue, float minVal, float maxVal, const char* unit);

    /**
     * @brief Dibuja la vista de escenario (Gig View) con textos gigantes.
     * @param presetNum Número de preset (0-99).
     * @param presetName Nombre del preset.
     * @param slotStates Array de 4 booleanos con el estado de los slots.
     * @param peak Pico de señal actual (0-8388607).
     */
    void drawGigView(uint8_t presetNum, const char* presetName, const bool* slotStates, int32_t peak);

    /**
     * @brief Limpia la pantalla completa de inmediato (solo usar en transiciones).
     */
    void clear();
    
    /**
     * @brief Envía el buffer de la pantalla por I2C.
     */
    void display();

    // Día 1 - Dirty Flags por Zona
    // Estructura de dirty flags (interna para DisplayManager)
    struct DirtyFlags {
        bool header : 1;
        bool body : 1;
        bool footer : 1;
    };

    /**
     * @brief Actualiza solo las zonas marcadas como dirty
     *
     * @param header Dirty flag para header (y: 0-15)
     * @param body Dirty flag para body (y: 16-47)
     * @param footer Dirty flag para footer (y: 48-63)
     *
     * Zonas:
     * - Header (y: 0-15): Preset name, bank
     * - Body (y: 16-47): Parameters, effects
     * - Footer (y: 48-63): Status, volume
     *
     * @note Solo envía por I2C las zonas dirty, ahorrando ~15ms por frame
     */
    void updateDisplay(bool header, bool body, bool footer);

private:
    Adafruit_SSD1306 oled;
};
