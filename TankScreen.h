#ifndef TANK_SCREEN_H
#define TANK_SCREEN_H

#include <TFT_eSPI.h>
#include "Config.h"

// 제어 모드
enum TankControlItem {
  TANK_CONTROL_NONE = -1,
  TANK_CONTROL_LIGHT = 0,
  TANK_CONTROL_PUMP,
  TANK_CONTROL_BACK,
  TANK_CONTROL_COUNT
};

class TankScreen {
private:
  TFT_eSPI* tft;
  SystemState* systemState; // Pointer to the global state
  unsigned long lastUpdate;
  bool needsFullRedraw;

  // 제어 상태
  TankControlItem selectedItem;
  bool editMode;

public:
  TankScreen(TFT_eSPI* display) :
    tft(display),
    systemState(nullptr),
    lastUpdate(0),
    needsFullRedraw(true),
    selectedItem(TANK_CONTROL_NONE),
    editMode(false) {}

  void setStateReference(SystemState* state) {
    systemState = state;
  }

  void begin() {
    selectedItem = TANK_CONTROL_LIGHT;
    editMode = false;
    needsFullRedraw = true;
  }

  void update() {
    if (millis() - lastUpdate < UI_PERIOD_MS) {
        return;
    }
    lastUpdate = millis();

    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (needsFullRedraw) {
            draw();
        } else {
            updateSensorValues();
            updateControls();
        }
        xSemaphoreGive(g_stateMutex);
    } else {
        Serial.println("TankScreen: Failed to get state mutex.");
    }
  }

  void forceRedraw() {
    needsFullRedraw = true;
  }

  void onEncoderRotate(int direction) {
    if (!systemState) return;

    if (editMode) {
      if (selectedItem == TANK_CONTROL_PUMP) {
        // This direct modification is not ideal in a multi-tasked env.
        // A better approach is to send a command to a handler task.
        // For now, we modify a temporary value or a copy.
        // Let's assume for now the control values are not part of the core SystemState
        // that gets updated by CAN, but are user-intent values.
        // int newOutput = systemState->tank.pumpOutput + (direction * 5);
        // if (newOutput < 0) newOutput = 0;
        // if (newOutput > 100) newOutput = 100;
        // systemState->tank.pumpOutput = newOutput;
        // updateControls();
      }
    } else {
      int newItem = (int)selectedItem + direction;
      if (newItem < 0) newItem = TANK_CONTROL_COUNT - 1;
      if (newItem >= TANK_CONTROL_COUNT) newItem = 0;
      selectedItem = (TankControlItem)newItem;
      
      if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        updateControls();
        xSemaphoreGive(g_stateMutex);
      }
    }
  }

  void onButtonClick() {
    if (selectedItem == TANK_CONTROL_BACK) {
      return;
    }
    if (!systemState) return;

    if (selectedItem == TANK_CONTROL_LIGHT) {
      // TODO: Send CAN command to toggle light
      // systemState->tank.lightOn = !systemState->tank.lightOn;
      Serial.println("TODO: Send CAN command for light");
    }

    if (selectedItem == TANK_CONTROL_PUMP) {
      editMode = !editMode;
      if (!editMode) {
        // TODO: Send CAN command to set pump output
        Serial.println("TODO: Send CAN command for pump");
      }
    }
    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        updateControls();
        xSemaphoreGive(g_stateMutex);
    }
  }

  TankControlItem getSelectedItem() const {
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
    tft->drawString("TANK DETAIL", 10, HEADER_HEIGHT / 2);
  }

  void drawSensorValues() {
    int x = 10;
    int y = HEADER_HEIGHT + 10;
    int lineHeight = 18;

    tft->setTextSize(1);
    tft->setTextColor(0x07FF);
    tft->setCursor(x, y);
    tft->print("=== SENSOR DATA ===");
    y += lineHeight + 5;

    if (systemState && systemState->comm.tank.ok) {
      tft->setTextColor(COLOR_TEXT);
      tft->setCursor(x, y);
      tft->printf("Temperature: %.1f C", systemState->tank.temp);
      y += lineHeight;

      tft->setCursor(x, y);
      tft->printf("Water Level: %.0f %%", systemState->tank.level);
      y += lineHeight;

      tft->setCursor(x, y);
      tft->printf("pH: %.1f", systemState->tank.ph);
      y += lineHeight;

      tft->setCursor(x, y);
      tft->printf("TDS: %d ppm", systemState->tank.tds);
      y += lineHeight;
      
      tft->setCursor(x, y);
      tft->printf("Turbidity: %.1f NTU", systemState->tank.turb);
      y += lineHeight;

      tft->setCursor(x, y);
      tft->printf("DO: %.1f mg/L", systemState->tank.dissolvedOxygen);

    } else {
      tft->setTextColor(COLOR_ERROR);
      tft->setTextDatum(MC_DATUM);
      tft->drawString("No Data Available", x + 80, y + 40);
    }
  }

  void drawControls() {
    int x = 170;
    int y = HEADER_HEIGHT + 10;
    int lineHeight = 18;
    int buttonHeight = 30;

    tft->setTextSize(1);
    tft->setTextColor(0x07FF);
    tft->setCursor(x, y);
    tft->print("=== CONTROLS ===");
    y += lineHeight + 5;

    if (!systemState) return;

    // A temporary state for UI controls that shouldn't be in the global state
    static bool lightControlState = false;
    static int pumpControlOutput = 0;

    // 조명 제어
    drawButton(x, y, 140, buttonHeight, "LIGHT",
               lightControlState ? "ON" : "OFF",
               selectedItem == TANK_CONTROL_LIGHT,
               lightControlState ? COLOR_OK : COLOR_INACTIVE);
    y += buttonHeight + 10;

    // 펌프 출력 제어
    drawPumpControl(x, y, 140, buttonHeight + 20,
                    selectedItem == TANK_CONTROL_PUMP, pumpControlOutput);
    y += buttonHeight + 30;

    // 뒤로 가기
    drawButton(x, y, 140, buttonHeight, "BACK",
               "",
               selectedItem == TANK_CONTROL_BACK,
               COLOR_INACTIVE);
  }

  void drawButton(int x, int y, int w, int h, const char* label,
                  const char* value, bool selected, uint16_t valueColor) {
    uint16_t borderColor = selected ? COLOR_WARNING : COLOR_TEXT;
    tft->fillRect(x, y, w, h, COLOR_PANEL_BG);
    tft->drawRect(x, y, w, h, borderColor);
    if (selected) {
      tft->drawRect(x + 1, y + 1, w - 2, h - 2, borderColor);
    }
    tft->setTextColor(COLOR_TEXT);
    tft->setTextSize(1);
    tft->setTextDatum(ML_DATUM);
    tft->drawString(label, x + 5, y + 8);
    if (strlen(value) > 0) {
      tft->setTextColor(valueColor);
      tft->setTextSize(2);
      tft->setTextDatum(ML_DATUM);
      tft->drawString(value, x + 5, y + 20);
    }
  }

  void drawPumpControl(int x, int y, int w, int h, bool selected, int pumpOutput) {
    uint16_t borderColor = selected ? COLOR_WARNING : COLOR_TEXT;
    tft->fillRect(x, y, w, h, COLOR_PANEL_BG);
    tft->drawRect(x, y, w, h, borderColor);
    if (selected) {
      tft->drawRect(x + 1, y + 1, w - 2, h - 2, borderColor);
    }
    tft->setTextColor(COLOR_TEXT);
    tft->setTextSize(1);
    tft->setTextDatum(ML_DATUM);
    tft->drawString("PUMP OUTPUT", x + 5, y + 8);
    tft->setTextColor(COLOR_OK);
    tft->setTextSize(2);
    tft->drawString(String(pumpOutput) + "%", x + 5, y + 25);
    if (editMode && selected) {
      tft->setTextColor(COLOR_WARNING);
      tft->setTextSize(1);
      tft->setTextDatum(MR_DATUM);
      tft->drawString("EDIT", x + w - 5, y + 25);
    }
    int barX = x + 5;
    int barY = y + h - 10;
    int barW = w - 10;
    int barH = 5;
    tft->drawRect(barX, barY, barW, barH, COLOR_TEXT);
    int fillW = (barW - 2) * pumpOutput / 100;
    tft->fillRect(barX + 1, barY + 1, fillW, barH - 2, COLOR_OK);
  }

  void drawFooter() {
    int y = SCREEN_HEIGHT - STATUS_BAR_HEIGHT;
    tft->fillRect(0, y, SCREEN_WIDTH, STATUS_BAR_HEIGHT, COLOR_PANEL_BG);
    tft->setTextSize(1);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextDatum(MC_DATUM);
    if (editMode) {
      tft->drawString("Rotate: Adjust | Click: Save", SCREEN_WIDTH / 2, y + STATUS_BAR_HEIGHT / 2);
    } else {
      tft->drawString("Rotate: Select | Click: Action", SCREEN_WIDTH / 2, y + STATUS_BAR_HEIGHT / 2);
    }
  }

  void updateSensorValues() {
    drawSensorValues();
  }

  void updateControls() {
    drawControls();
    drawFooter();
  }
};

#endif // TANK_SCREEN_H
