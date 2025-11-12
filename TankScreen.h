#ifndef TANK_SCREEN_H
#define TANK_SCREEN_H

#include <TFT_eSPI.h>
#include "Config.h"
#include "CANHandler.h"

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
  SystemState* systemState;
  CANHandler* canHandler;
  unsigned long lastUpdate;
  bool needsFullRedraw;

  // 제어 상태
  TankControlItem selectedItem;
  bool editMode;
  
  // UI 상태 변수
  bool ui_lightOn;
  int ui_pumpOutput;

public:
  TankScreen(TFT_eSPI* display) :
    tft(display),
    systemState(nullptr),
    canHandler(nullptr),
    lastUpdate(0),
    needsFullRedraw(true),
    selectedItem(TANK_CONTROL_NONE),
    editMode(false),
    ui_lightOn(false),
    ui_pumpOutput(0) {}

  void setStateReference(SystemState* state) {
    systemState = state;
  }

  void setCANHandler(CANHandler* handler) {
    canHandler = handler;
  }

  void begin() {
    selectedItem = TANK_CONTROL_LIGHT;
    editMode = false;
    needsFullRedraw = true;
    // 화면 진입 시, 전역 상태 값으로 UI 상태 초기화
    if (systemState && xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        // These values should represent the actual hardware state
        // ui_lightOn = systemState->tank.lightOn; 
        // ui_pumpOutput = systemState->tank.pumpOutput;
        xSemaphoreGive(g_stateMutex);
    }
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
    } else {
        Serial.println("TankScreen: Failed to get state mutex.");
    }
  }

  void onEncoderRotate(int direction) {
    if (editMode) {
      if (selectedItem == TANK_CONTROL_PUMP) {
        ui_pumpOutput += (direction * 5);
        if (ui_pumpOutput < 0) ui_pumpOutput = 0;
        if (ui_pumpOutput > 100) ui_pumpOutput = 100;
      }
    } else {
      int newItem = (int)selectedItem + direction;
      if (newItem < 0) newItem = TANK_CONTROL_COUNT - 1;
      if (newItem >= TANK_CONTROL_COUNT) newItem = 0;
      selectedItem = (TankControlItem)newItem;
    }
    needsFullRedraw = true;
  }

  void onButtonClick() {
    if (selectedItem == TANK_CONTROL_BACK) {
      if(editMode) editMode = false;
      return;
    }
    if (!canHandler) return;

    if (selectedItem == TANK_CONTROL_LIGHT) {
      ui_lightOn = !ui_lightOn;
      // TODO: Add setTankLight to CANHandler and uncomment
      // canHandler->setTankLight(ui_lightOn);
      Serial.printf("CAN CMD: Set Light %s\n", ui_lightOn ? "ON" : "OFF");
    }

    if (selectedItem == TANK_CONTROL_PUMP) {
      editMode = !editMode;
      if (!editMode) { // On exiting edit mode, send command
        // TODO: Add setTankPump to CANHandler and uncomment
        // canHandler->setTankPump(ui_pumpOutput);
        Serial.printf("CAN CMD: Set Pump to %d%%\n", ui_pumpOutput);
      }
    }
    needsFullRedraw = true;
  }

  TankControlItem getSelectedItem() const { return selectedItem; }
  bool isEditMode() const { return editMode; }

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
    int x = 10, y = HEADER_HEIGHT + 10, lineHeight = 18;
    tft->setTextSize(1);
    tft->setTextColor(0x07FF);
    tft->setCursor(x, y);
    tft->print("=== SENSOR DATA ===");
    y += lineHeight + 5;

    if (systemState && systemState->comm.tank.ok) {
      tft->setTextColor(COLOR_TEXT);
      tft->setCursor(x, y); tft->printf("Temperature: %.1f C", systemState->tank.temp); y += lineHeight;
      tft->setCursor(x, y); tft->printf("Water Level: %.0f %%", systemState->tank.level); y += lineHeight;
      tft->setCursor(x, y); tft->printf("pH: %.1f", systemState->tank.ph); y += lineHeight;
      tft->setCursor(x, y); tft->printf("TDS: %d ppm", systemState->tank.tds); y += lineHeight;
      tft->setCursor(x, y); tft->printf("Turbidity: %.1f NTU", systemState->tank.turb); y += lineHeight;
      tft->setCursor(x, y); tft->printf("DO: %.1f mg/L", systemState->tank.dissolvedOxygen);
    } else {
      tft->setTextColor(COLOR_ERROR);
      tft->setTextDatum(MC_DATUM);
      tft->drawString("No Data Available", x + 80, y + 40);
      tft->setTextDatum(ML_DATUM);
    }
  }

  void drawControls() {
    int x = 170, y = HEADER_HEIGHT + 10, buttonHeight = 30;
    tft->setTextSize(1);
    tft->setTextColor(0x07FF);
    tft->setCursor(x, y);
    tft->print("=== CONTROLS ===");
    y += 23;

    drawButton(x, y, 140, buttonHeight, "LIGHT", ui_lightOn ? "ON" : "OFF", selectedItem == TANK_CONTROL_LIGHT, ui_lightOn ? COLOR_OK : COLOR_INACTIVE);
    y += buttonHeight + 10;
    drawPumpControl(x, y, 140, buttonHeight + 20, selectedItem == TANK_CONTROL_PUMP);
    y += buttonHeight + 30;
    drawButton(x, y, 140, buttonHeight, "BACK", "", selectedItem == TANK_CONTROL_BACK, COLOR_INACTIVE);
  }

  void drawButton(int x, int y, int w, int h, const char* label, const char* value, bool selected, uint16_t valueColor) {
    uint16_t borderColor = selected ? COLOR_WARNING : COLOR_TEXT;
    tft->fillRect(x, y, w, h, COLOR_PANEL_BG);
    tft->drawRect(x, y, w, h, borderColor);
    if (selected) tft->drawRect(x + 1, y + 1, w - 2, h - 2, borderColor);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextSize(1);
    tft->setTextDatum(ML_DATUM);
    tft->drawString(label, x + 5, y + 8);
    if (strlen(value) > 0) {
      tft->setTextColor(valueColor);
      tft->setTextSize(2);
      tft->drawString(value, x + 5, y + 20);
    }
  }

  void drawPumpControl(int x, int y, int w, int h, bool selected) {
    uint16_t borderColor = selected ? COLOR_WARNING : COLOR_TEXT;
    tft->fillRect(x, y, w, h, COLOR_PANEL_BG);
    tft->drawRect(x, y, w, h, borderColor);
    if (selected) tft->drawRect(x + 1, y + 1, w - 2, h - 2, borderColor);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextSize(1);
    tft->setTextDatum(ML_DATUM);
    tft->drawString("PUMP OUTPUT", x + 5, y + 8);
    tft->setTextColor(COLOR_OK);
    tft->setTextSize(2);
    tft->drawString(String(ui_pumpOutput) + "%", x + 5, y + 25);
    if (editMode && selected) {
      tft->setTextColor(COLOR_WARNING);
      tft->setTextSize(1);
      tft->setTextDatum(MR_DATUM);
      tft->drawString("EDIT", x + w - 5, y + 25);
    }
    int barX = x + 5, barY = y + h - 10, barW = w - 10, barH = 5;
    tft->drawRect(barX, barY, barW, barH, COLOR_TEXT);
    int fillW = (barW - 2) * ui_pumpOutput / 100;
    tft->fillRect(barX + 1, barY + 1, fillW, barH - 2, COLOR_OK);
    tft->setTextDatum(ML_DATUM);
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

  void updateSensorValues() { drawSensorValues(); }
  void updateControls() { drawControls(); drawFooter(); }
};

#endif // TANK_SCREEN_H