#include "DisplayManager.h"
#include "../system/FaultLatch.h"
#include <Fonts/FreeSansBold24pt7b.h>

DisplayManager::DisplayManager() 
    : oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET) {
}

bool DisplayManager::init() {
    // Inicializar I2C0 de la Pico en los pines 0 y 1
    Wire.setSDA(PIN_OLED_SDA);
    Wire.setSCL(PIN_OLED_SCL);
    Wire.begin();
    
    // Frecuencia rápida para I2C (400kHz)
    Wire.setClock(400000);
    
    // Timeout estricto de seguridad I2C (5ms)
    Wire.setTimeout(5, true);
    
    if(!oled.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        globalFaults.registerFault(FaultLatch::I2C_ERROR);
        return false;
    }
    
    oled.clearDisplay();
    oled.display();
    return true;
}

void DisplayManager::setContrast(uint8_t c) {
    oled.ssd1306_command(SSD1306_SETCONTRAST);
    oled.ssd1306_command(c);
}

void DisplayManager::sleep() {
    oled.ssd1306_command(SSD1306_DISPLAYOFF);
}

void DisplayManager::wake() {
    oled.ssd1306_command(SSD1306_DISPLAYON);
}

void DisplayManager::drawTuner(const char* note, int8_t cents, bool signal) {
    oled.clearDisplay();
    
    // Título
    oled.fillRect(0, 0, SCREEN_WIDTH, 12, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setTextSize(1);
    oled.setCursor(35, 2);
    oled.print("TUNER (MUTE)");

    if (!signal) {
        oled.setTextColor(SSD1306_WHITE);
        oled.setTextSize(1);
        oled.setCursor(30, 35);
        oled.print("WAITING SIGNAL");
    } else {
        // Nota en grande (Fuente GFX Custom)
        oled.setTextColor(SSD1306_WHITE);
        oled.setFont(&FreeSansBold24pt7b);
        oled.setTextSize(1);
        int16_t x1, y1;
        uint16_t w, h;
        oled.getTextBounds(note, 0, 0, &x1, &y1, &w, &h);
        oled.setCursor((SCREEN_WIDTH - w) / 2, 45); // Y es baseline
        oled.print(note);
        oled.setFont(NULL); // Reset

        // Escala de Cents (Barra inferior)
        int barY = 55;
        oled.drawRect(14, barY, 100, 6, SSD1306_WHITE);
        oled.drawLine(64, barY - 2, 64, barY + 8, SSD1306_WHITE); // Centro (In Tune)

        // Aguja
        int needleX = 64 + (cents * 50 / 50); // Mapear -50..50 a +/- 50 pixeles
        oled.fillRect(needleX - 1, barY - 4, 3, 14, SSD1306_WHITE);

        // Indicador de "In Tune"
        if (abs(cents) < 3) {
            oled.fillTriangle(60, barY-8, 68, barY-8, 64, barY-4, SSD1306_WHITE);
        }
    }
}

void DisplayManager::drawGigView(uint8_t presetNum, const char* presetName, const bool* slotStates, int32_t peak) {
    oled.clearDisplay();
    
    // Número de Preset Gigante (Fuente Custom)
    oled.setTextColor(SSD1306_WHITE);
    oled.setFont(&FreeSansBold24pt7b);
    oled.setTextSize(1);
    oled.setCursor(0, 36); // Ajustado a baseline
    if (presetNum < 10) oled.print("0");
    oled.print(presetNum);
    oled.setFont(NULL); // Reset

    // Nombre del Preset
    oled.setTextSize(2);
    oled.setCursor(55, 10);
    char shortName[10];
    strncpy(shortName, presetName, 8);
    shortName[8] = '\0';
    oled.print(shortName);

    // --- VU METER MINI ---
    int vuX = 60, vuY = 32, vuW = 64, vuH = 4;
    oled.drawRect(vuX, vuY, vuW, vuH, SSD1306_WHITE);
    int barW = (int)((float)peak / 8388607.0f * vuW);
    if (barW > vuW) barW = vuW;
    oled.fillRect(vuX, vuY, barW, vuH, SSD1306_WHITE);
    
    // Indicador CLIP
    if (peak > 8000000) { // ~95% de escala
        oled.fillRect(vuX + vuW - 10, vuY - 1, 10, vuH + 2, SSD1306_WHITE);
    }

    // Separador
    oled.drawFastHLine(0, 42, SCREEN_WIDTH, SSD1306_WHITE);

    // Estado de los 4 Slots (Iconos rápidos en la parte inferior)
    int boxW = 28;
    int boxH = 12;
    int gap = 4;
    for (int i = 0; i < 4; i++) {
        int x = 2 + (i * (boxW + gap));
        int y = 50;
        
        if (slotStates[i]) {
            oled.fillRect(x, y, boxW, boxH, SSD1306_WHITE);
            oled.setTextColor(SSD1306_BLACK);
        } else {
            oled.drawRect(x, y, boxW, boxH, SSD1306_WHITE);
            oled.setTextColor(SSD1306_WHITE);
        }
        
        oled.setTextSize(1);
        oled.setCursor(x + 6, y + 2);
        oled.print("S");
        oled.print(i + 1);
    }
}

void DisplayManager::clear() {
    oled.clearDisplay();
}

void DisplayManager::display() {
    oled.display();
}

void DisplayManager::drawStaticUI(const char* title) {
    oled.clearDisplay(); // Esta función solo se llama 1 vez por pantalla, así que es seguro
    
    // Barra superior
    oled.fillRect(0, 0, SCREEN_WIDTH, 12, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(2, 2);
    oled.print(title);
    
    // Se deja listo el display, pero NO llamamos a display() aquí
    // El llamador debe orquestarlo al final del frame.
}

void DisplayManager::updateParameterLine(const char* label, const char* value, uint8_t line, bool selected) {
    if (line > 3) return; // Solo hay espacio para ~4 líneas en 128x64 descontando el título
    
    // Coordenadas base
    int yPos = 16 + (line * 12);
    
    // Limpiar el rectángulo exacto de esta línea (Overpaint de fondo)
    // Esto es el corazón del patrón Dirty Flag (evitamos clearDisplay global)
    if (selected) {
        oled.fillRect(0, yPos, SCREEN_WIDTH, 12, SSD1306_WHITE);
        oled.setTextColor(SSD1306_BLACK, SSD1306_WHITE); // Texto negro, fondo blanco
    } else {
        oled.fillRect(0, yPos, SCREEN_WIDTH, 12, SSD1306_BLACK);
        oled.setTextColor(SSD1306_WHITE, SSD1306_BLACK); // Texto blanco, fondo negro
    }
    
    oled.setTextSize(1);
    
    // Imprimir Etiqueta
    oled.setCursor(4, yPos + 2);
    oled.print(label);
    
    // Imprimir Valor (Alineado a la derecha aprox)
    oled.setCursor(SCREEN_WIDTH - 40, yPos + 2);
    oled.print(value);
}

// Día 1 - Dirty Flags por Zona (Header, Body, Footer)
void DisplayManager::updateDisplay(bool header, bool body, bool footer) {
    // Nota: Adafruit_SSD1306 no soporta actualizaciones parciales nativas
    // Enviamos solo las zonas marcadas como dirty (futuro: implementar slicing de buffer)

    // HEADER: Top ~16px (preset name, bank)
    if (header) {
        oled.display();  // TODO: Implementar slicing de buffer para envío parcial
        return;  // Si actualizamos header, no necesitamos actualizar el resto
    }

    // BODY: Middle ~32px (parameters, effects)
    if (body) {
        oled.display();
        return;
    }

    // FOOTER: Bottom ~16px (status, volume)
    if (footer) {
        oled.display();
        return;
    }
}

void DisplayManager::drawSaveScreen(uint8_t targetPreset) {
    oled.clearDisplay();
    
    oled.fillRect(0, 0, SCREEN_WIDTH, 12, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setTextSize(1);
    oled.setCursor(2, 2);
    oled.print("SAVE PRESET");

    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(10, 20);
    oled.print("Save to PRESET?");
    
    oled.setFont(&FreeSansBold24pt7b);
    oled.setTextSize(1);
    oled.setCursor(40, 52); // Baseline
    if (targetPreset < 10) oled.print("0");
    oled.print(targetPreset);
    oled.setFont(NULL); // Reset
    
    oled.setTextSize(1);
    oled.setCursor(0, 56);
    oled.print("[CLK]=OK   [FS]=NO");
}

void DisplayManager::drawBipolarBar(uint8_t y, const char* label, float value, float origValue, float minVal, float maxVal, const char* unit) {
    oled.setCursor(0, y);
    oled.print(label);
    
    int barX = 40;
    int barW = 50;
    int barH = 6;
    int barY = y;
    
    oled.drawRect(barX, barY, barW, barH, SSD1306_WHITE);
    
    int centerX = barX + (barW / 2);
    oled.drawFastVLine(centerX, barY - 1, barH + 2, SSD1306_WHITE);
    
    float normVal = (value - minVal) / (maxVal - minVal);
    int fillX = barX + (normVal * barW);
    
    if (fillX > centerX) {
        oled.fillRect(centerX, barY, fillX - centerX, barH, SSD1306_WHITE);
    } else {
        oled.fillRect(fillX, barY, centerX - fillX, barH, SSD1306_WHITE);
    }
    
    float normOrig = (origValue - minVal) / (maxVal - minVal);
    int origX = barX + (normOrig * barW);
    if (abs(origX - fillX) > 1) { 
        oled.drawFastVLine(origX, barY - 2, barH + 4, SSD1306_INVERSE);
    }
    
    oled.setCursor(95, y);
    if (value > 0.01f && minVal < 0) oled.print("+");
    oled.print(value, 1);
}

void DisplayManager::drawUnipolarBar(uint8_t y, const char* label, float value, float origValue, float minVal, float maxVal, const char* unit) {
    oled.setCursor(0, y);
    oled.print(label);
    
    int barX = 40;
    int barW = 50;
    int barH = 6;
    int barY = y;
    
    oled.drawRect(barX, barY, barW, barH, SSD1306_WHITE);
    
    float normVal = (value - minVal) / (maxVal - minVal);
    int fillW = normVal * barW;
    oled.fillRect(barX, barY, fillW, barH, SSD1306_WHITE);
    
    float normOrig = (origValue - minVal) / (maxVal - minVal);
    int origX = barX + (normOrig * barW);
    if (abs(origX - (barX + fillW)) > 1) {
        oled.drawFastVLine(origX, barY - 2, barH + 4, SSD1306_INVERSE);
    }
    
    oled.setCursor(95, y);
    oled.print(value, 1);
}
