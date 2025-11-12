#ifndef NUTRIENT_SCREEN_H
#define NUTRIENT_SCREEN_H

#include <TFT_eSPI.h>
#include "Config.h"
#include "CANHandler.h"

enum NutrientControlItem {
  NUTRIENT_CONTROL_NONE = -1,
  NUTRIENT_CONTROL_PUMP_A,
  NUTRIENT_CONTROL_PUMP_B,
  NUTRIENT_CONTROL_PUMP_C,
  NUTRIENT_CONTROL_PUMP_D,
  NUTRIENT_CONTROL_BACK,
  NUTRIENT_CONTROL_COUNT
};

class NutrientScreen {
private:
  TFT_eSPI* tft;
  SystemState* systemState;
  CANHandler* canHandler;
  unsigned long lastUpdate;
  bool needsFullRedraw;

  NutrientControlItem selectedItem;

public:
  NutrientScreen(TFT_eSPI* display) :
    tft(display),
    systemState(nullptr),
    canHandler(nullptr),
    lastUpdate(0),
    needsFullRedraw(true),
    selectedItem(NUTRIENT_CONTROL_PUMP_A) {}

  void setStateReference(SystemState* state) {
    systemState = state;
  }

  void setCANHandler(CANHandler* handler) {
    canHandler = handler;
  }

  void begin() {
    selectedItem = NUTRIENT_CONTROL_PUMP_A;
    needsFullRedraw = true;
  }

  void update() {
    if (millis() - lastUpdate < UI_PERIOD_MS) return;
    lastUpdate = millis();

    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      if (needsFullRedraw) {
        draw();
      } else {
        updateSensorValues();
        updateControls();
      }
      xSemaphoreGive(g_stateMutex);
    }
  }

  void onEncoderRotate(int direction) {
    int newItem = (int)selectedItem + direction;
    if (newItem < 0) newItem = NUTRIENT_CONTROL_COUNT - 1;
    if (newItem >= NUTRIENT_CONTROL_COUNT) newItem = 0;
    selectedItem = (NutrientControlItem)newItem;
    needsFullRedraw = true; // Simple redraw for this screen
  }

  void onButtonClick() {
    if (selectedItem == NUTRIENT_CONTROL_BACK || !canHandler) {
      return;
    }
    
    // For this screen, a click just toggles the pump for a moment (sends ON command)
    // The pump should be designed to turn off automatically after a short duration.
    bool pumpState = true; // Always turn ON
    if (selectedItem == NUTRIENT_CONTROL_PUMP_A) {
        canHandler->setNutrientPump(pumpState); // Using a generic command for now
        Serial.println("CAN CMD: Turn on Pump A");
    } else if (selectedItem == NUTRIENT_CONTROL_PUMP_B) {
        // TODO: Add specific pump commands if needed
        Serial.println("CAN CMD: Turn on Pump B");
    } else if (selectedItem == NUTRIENT_CONTROL_PUMP_C) {
        Serial.println("CAN CMD: Turn on Pump C");
    } else if (selectedItem == NUTRIENT_CONTROL_PUMP_D) {
        Serial.println("CAN CMD: Turn on Pump D");
    }
    needsFullRedraw = true;
  }

  NutrientControlItem getSelectedItem() const {
    return selectedItem;
  }

private:
  void draw() {
    tft->fillScreen(COLOR_BACKGROUND);
    drawHeader();
    drawSensorValues();
    drawControls();
    drawFooter();
    needsFullRedraw = false;
  }

  void drawHeader() {
    tft->fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_HEADER_BG);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextSize(2);
    tft->setTextDatum(ML_DATUM);
    tft->drawString("NUTRIENT DETAIL", 10, HEADER_HEIGHT / 2);
  }

  void drawSensorValues() {
    int x = 10, y = HEADER_HEIGHT + 10, lineHeight = 18;
    tft->setTextSize(1);
    tft->setTextColor(0x07FF, COLOR_BACKGROUND);
    tft->setCursor(x, y);
    tft->print("=== NUTRIENT LEVELS ===");
    y += lineHeight + 5;

    if (systemState && systemState->comm.nutri.ok) {
      tft->setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
      tft->setCursor(x, y); tft->printf("Nutrient A: %d ml", systemState->nutri.remain_ml.A); y += lineHeight;
      tft->setCursor(x, y); tft->printf("Nutrient B: %d ml", systemState->nutri.remain_ml.B); y += lineHeight;
      tft->setCursor(x, y); tft->printf("Nutrient C: %d ml", systemState->nutri.remain_ml.C); y += lineHeight;
      tft->setCursor(x, y); tft->printf("Nutrient D: %d ml", systemState->nutri.remain_ml.D); y += lineHeight;
    } else {
      tft->setTextColor(COLOR_ERROR, COLOR_BACKGROUND);
      tft->setTextDatum(MC_DATUM);
      tft->drawString("No Data Available", x + 80, y + 40);
      tft->setTextDatum(ML_DATUM);
    }
  }

  void drawControls() {
    int x = 170, y = HEADER_HEIGHT + 10, buttonHeight = 28, spacing = 5;
    tft->setTextSize(1);
    tft->setTextColor(0x07FF);
    tft->setCursor(x, y);
    tft->print("=== CONTROLS ===");
    y += 23;

    drawButton(x, y, 140, buttonHeight, "PUMP A", "RUN", selectedItem == NUTRIENT_CONTROL_PUMP_A);
    y += buttonHeight + spacing;
    drawButton(x, y, 140, buttonHeight, "PUMP B", "RUN", selectedItem == NUTRIENT_CONTROL_PUMP_B);
    y += buttonHeight + spacing;
    drawButton(x, y, 140, buttonHeight, "PUMP C", "RUN", selectedItem == NUTRIENT_CONTROL_PUMP_C);
    y += buttonHeight + spacing;
    drawButton(x, y, 140, buttonHeight, "PUMP D", "RUN", selectedItem == NUTRIENT_CONTROL_PUMP_D);
    y += buttonHeight + spacing;
    drawButton(x, y, 140, buttonHeight, "BACK", "", selectedItem == NUTRIENT_CONTROL_BACK);
  }

  void drawButton(int x, int y, int w, int h, const char* label, const char* value, bool selected) {
    uint16_t borderColor = selected ? COLOR_WARNING : COLOR_TEXT;
    tft->fillRect(x, y, w, h, COLOR_PANEL_BG);
    tft->drawRect(x, y, w, h, borderColor);
    if (selected) tft->drawRect(x + 1, y + 1, w - 2, h - 2, borderColor);
    
    tft->setTextColor(COLOR_TEXT, COLOR_PANEL_BG);
    tft->setTextSize(2);
    tft->setTextDatum(ML_DATUM);
    tft->drawString(label, x + 5, y + h / 2);

    if (strlen(value) > 0) {
      tft->setTextColor(COLOR_OK, COLOR_PANEL_BG);
      tft->setTextDatum(MR_DATUM);
      tft->drawString(value, x + w - 5, y + h / 2);
    }
    tft->setTextDatum(ML_DATUM);
  }

  void drawFooter() {
    int y = SCREEN_HEIGHT - STATUS_BAR_HEIGHT;
    tft->fillRect(0, y, SCREEN_WIDTH, STATUS_BAR_HEIGHT, COLOR_PANEL_BG);
    tft->setTextSize(1);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("Rotate: Select | Click: Run Pump", SCREEN_WIDTH / 2, y + STATUS_BAR_HEIGHT / 2);
  }

  void updateSensorValues() { drawSensorValues(); }
  void updateControls() { drawControls(); }
};

#endif // NUTRIENT_SCREEN_H