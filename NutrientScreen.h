#ifndef NUTRIENT_SCREEN_H
#define NUTRIENT_SCREEN_H

#include <TFT_eSPI.h>
#include "Config.h"

// 제어 모드
enum NutrientControlItem {
  NUTRIENT_CONTROL_NONE = -1,
  NUTRIENT_CONTROL_SUPPLY = 0,
  NUTRIENT_CONTROL_STOP,
  NUTRIENT_CONTROL_BACK,
  NUTRIENT_CONTROL_COUNT
};

// 양액기 상태
enum NutrientStatus {
  NUTRIENT_IDLE = 0,
  NUTRIENT_SUPPLYING,
  NUTRIENT_STOPPED
};

class NutrientScreen {
private:
  TFT_eSPI* tft;
  SystemState* systemState;
  unsigned long lastUpdate;
  bool needsFullRedraw;

  // 제어 상태
  NutrientControlItem selectedItem;
  NutrientStatus operationStatus;

public:
  NutrientScreen(TFT_eSPI* display) :
    tft(display),
    systemState(nullptr),
    lastUpdate(0),
    needsFullRedraw(true),
    selectedItem(NUTRIENT_CONTROL_NONE),
    operationStatus(NUTRIENT_IDLE) {}

  void setStateReference(SystemState* state) {
    systemState = state;
  }

  void begin() {
    selectedItem = NUTRIENT_CONTROL_SUPPLY;
    operationStatus = NUTRIENT_IDLE;
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
            updateSystemInfo();
            updateStepperStatus();
            updateControls();
        }
        xSemaphoreGive(g_stateMutex);
    } else {
        Serial.println("NutrientScreen: Failed to get state mutex.");
    }
  }

  void forceRedraw() {
    needsFullRedraw = true;
  }

  void onEncoderRotate(int direction) {
    int newItem = (int)selectedItem + direction;
    if (newItem < 0) newItem = NUTRIENT_CONTROL_COUNT - 1;
    if (newItem >= NUTRIENT_CONTROL_COUNT) newItem = 0;
    selectedItem = (NutrientControlItem)newItem;
    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        updateControls();
        xSemaphoreGive(g_stateMutex);
    }
  }

  void onButtonClick() {
    if (selectedItem == NUTRIENT_CONTROL_BACK) return;
    if (selectedItem == NUTRIENT_CONTROL_SUPPLY) {
      operationStatus = NUTRIENT_SUPPLYING;
      // TODO: Send CAN command
    }
    if (selectedItem == NUTRIENT_CONTROL_STOP) {
      operationStatus = NUTRIENT_STOPPED;
      // TODO: Send CAN command
    }
    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        updateControls();
        xSemaphoreGive(g_stateMutex);
    }
  }

  NutrientControlItem getSelectedItem() const {
    return selectedItem;
  }

  NutrientStatus getOperationStatus() const {
    return operationStatus;
  }

private:
  void draw() {
    tft->fillScreen(COLOR_BACKGROUND);
    drawHeader();
    drawSystemInfo();
    drawStepperStatus();
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

  void drawSystemInfo() {
    int x = 10;
    int y = HEADER_HEIGHT + 10;
    int lineHeight = 18;

    tft->setTextSize(1);
    tft->setTextColor(0x07FF);
    tft->setCursor(x, y);
    tft->print("=== REMAINING (ml) ===");
    y += lineHeight + 5;

    if (systemState && systemState->comm.nutri.ok) {
      tft->setTextColor(COLOR_TEXT);
      tft->setCursor(x, y);
      tft->printf("Channel A: %d", systemState->nutri.remain_ml.A);
      y += lineHeight;

      tft->setCursor(x, y);
      tft->printf("Channel B: %d", systemState->nutri.remain_ml.B);
      y += lineHeight;

      tft->setCursor(x, y);
      tft->printf("Channel C: %d", systemState->nutri.remain_ml.C);
      y += lineHeight;

      tft->setCursor(x, y);
      tft->printf("Channel D: %d", systemState->nutri.remain_ml.D);
      y += lineHeight;

    } else {
      tft->setTextColor(COLOR_ERROR);
      tft->setTextDatum(MC_DATUM);
      tft->drawString("No Data Available", x + 80, y + 40);
    }
  }

  void drawStepperStatus() {
    // This info is not in the new SystemState, so we can't display it.
    // This section can be removed or adapted if the state is updated.
  }

  void drawControls() {
    int x = 180;
    int y = HEADER_HEIGHT + 10;
    int lineHeight = 18;
    int buttonHeight = 35;

    tft->setTextSize(1);
    tft->setTextColor(0x07FF);
    tft->setCursor(x, y);
    tft->print("=== CONTROLS ===");
    y += lineHeight + 5;

    y += 5;
    tft->setTextColor(COLOR_TEXT);
    tft->setCursor(x, y);
    tft->print("Status:");
    y += 12;
    tft->setCursor(x, y);

    switch (operationStatus) {
      case NUTRIENT_IDLE: tft->setTextColor(COLOR_INACTIVE); tft->print("IDLE"); break;
      case NUTRIENT_SUPPLYING: tft->setTextColor(COLOR_OK); tft->print("SUPPLYING"); break;
      case NUTRIENT_STOPPED: tft->setTextColor(COLOR_WARNING); tft->print("STOPPED"); break;
    }
    y += lineHeight + 10;

    drawButton(x, y, 130, buttonHeight, "SUPPLY", operationStatus == NUTRIENT_SUPPLYING ? "RUNNING" : "START", selectedItem == NUTRIENT_CONTROL_SUPPLY, operationStatus == NUTRIENT_SUPPLYING ? COLOR_OK : COLOR_TEXT);
    y += buttonHeight + 10;

    drawButton(x, y, 130, buttonHeight, "STOP", "", selectedItem == NUTRIENT_CONTROL_STOP, COLOR_ERROR);
    y += buttonHeight + 10;

    drawButton(x, y, 130, buttonHeight, "BACK", "", selectedItem == NUTRIENT_CONTROL_BACK, COLOR_INACTIVE);
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
      tft->setTextDatum(ML_DATUM);
      tft->drawString(value, x + 5, y + 25);
    }
  }

  void drawFooter() {
    int y = SCREEN_HEIGHT - STATUS_BAR_HEIGHT;
    tft->fillRect(0, y, SCREEN_WIDTH, STATUS_BAR_HEIGHT, COLOR_PANEL_BG);
    tft->setTextSize(1);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("Rotate: Select | Click: Action", SCREEN_WIDTH / 2, y + STATUS_BAR_HEIGHT / 2);
  }

  void updateSystemInfo() { drawSystemInfo(); }
  void updateStepperStatus() { drawStepperStatus(); }
  void updateControls() { drawControls(); drawFooter(); }
};

#endif // NUTRIENT_SCREEN_H
