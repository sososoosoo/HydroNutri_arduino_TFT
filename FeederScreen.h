#ifndef FEEDER_SCREEN_H
#define FEEDER_SCREEN_H

#include <TFT_eSPI.h>
#include "Config.h"
#include "CANHandler.h"

enum FeederControlItem {
  FEEDER_CONTROL_NONE = -1,
  FEEDER_CONTROL_FEED_NOW,
  FEEDER_CONTROL_SCHEDULE_1,
  FEEDER_CONTROL_SCHEDULE_2,
  FEEDER_CONTROL_SCHEDULE_3,
  FEEDER_CONTROL_BACK,
  FEEDER_CONTROL_COUNT
};

class FeederScreen {
private:
  TFT_eSPI* tft;
  SystemState* systemState;
  CANHandler* canHandler;
  unsigned long lastUpdate;
  bool needsFullRedraw;

  FeederControlItem selectedItem;
  bool editMode;
  
  // UI temporary state
  int feedAmount; // in grams

public:
  FeederScreen(TFT_eSPI* display) :
    tft(display),
    systemState(nullptr),
    canHandler(nullptr),
    lastUpdate(0),
    needsFullRedraw(true),
    selectedItem(FEEDER_CONTROL_FEED_NOW),
    editMode(false),
    feedAmount(10) {}

  void setStateReference(SystemState* state) {
    systemState = state;
  }

  void setCANHandler(CANHandler* handler) {
    canHandler = handler;
  }

  void begin() {
    selectedItem = FEEDER_CONTROL_FEED_NOW;
    editMode = false;
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
    if (editMode) {
      if (selectedItem == FEEDER_CONTROL_FEED_NOW) {
        feedAmount += direction;
        if (feedAmount < 1) feedAmount = 1;
        if (feedAmount > 50) feedAmount = 50;
      }
      // TODO: Add schedule editing logic
    } else {
      int newItem = (int)selectedItem + direction;
      if (newItem < 0) newItem = FEEDER_CONTROL_COUNT - 1;
      if (newItem >= FEEDER_CONTROL_COUNT) newItem = 0;
      selectedItem = (FeederControlItem)newItem;
    }
    needsFullRedraw = true;
  }

  void onButtonClick() {
    if (selectedItem == FEEDER_CONTROL_BACK) {
      if (editMode) editMode = false;
      return;
    }
    if (!canHandler) return;

    if (selectedItem == FEEDER_CONTROL_FEED_NOW) {
      editMode = !editMode;
      if (!editMode) {
        canHandler->feedNow(feedAmount);
        Serial.printf("CAN CMD: Feed Now %d grams\n", feedAmount);
      }
    }
    // TODO: Add schedule editing logic
    needsFullRedraw = true;
  }

  FeederControlItem getSelectedItem() const { return selectedItem; }
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
    tft->drawString("FEEDER DETAIL", 10, HEADER_HEIGHT / 2);
  }

  void drawSensorValues() {
    int x = 10, y = HEADER_HEIGHT + 10, lineHeight = 18;
    tft->setTextSize(1);
    tft->setTextColor(0x07FF, COLOR_BACKGROUND);
    tft->setCursor(x, y);
    tft->print("=== SENSOR DATA ===");
    y += lineHeight + 5;

    if (systemState && systemState->comm.feed.ok) {
      tft->setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
      tft->setCursor(x, y); tft->printf("Remaining: %d g", systemState->feed.remain_g); y += lineHeight;
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

    drawFeedNowControl(x, y, 140, buttonHeight, selectedItem == FEEDER_CONTROL_FEED_NOW);
    y += buttonHeight + spacing;
    
    // Placeholder for schedule buttons
    drawButton(x, y, 140, buttonHeight, "SCHEDULE 1", "07:30", selectedItem == FEEDER_CONTROL_SCHEDULE_1, false);
    y += buttonHeight + spacing;
    drawButton(x, y, 140, buttonHeight, "SCHEDULE 2", "12:00", selectedItem == FEEDER_CONTROL_SCHEDULE_2, false);
    y += buttonHeight + spacing;
    drawButton(x, y, 140, buttonHeight, "SCHEDULE 3", "18:30", selectedItem == FEEDER_CONTROL_SCHEDULE_3, false);
    y += buttonHeight + spacing;

    drawButton(x, y, 140, buttonHeight, "BACK", "", selectedItem == FEEDER_CONTROL_BACK, false);
  }

  void drawButton(int x, int y, int w, int h, const char* label, const char* value, bool selected, bool isEditing) {
    uint16_t borderColor = selected ? COLOR_WARNING : COLOR_TEXT;
    tft->fillRect(x, y, w, h, COLOR_PANEL_BG);
    tft->drawRect(x, y, w, h, borderColor);
    if (selected) tft->drawRect(x + 1, y + 1, w - 2, h - 2, borderColor);
    
    tft->setTextColor(COLOR_TEXT, COLOR_PANEL_BG);
    tft->setTextSize(1);
    tft->setTextDatum(ML_DATUM);
    tft->drawString(label, x + 5, y + h / 2);

    if (strlen(value) > 0) {
      tft->setTextColor(isEditing ? COLOR_WARNING : COLOR_OK, COLOR_PANEL_BG);
      tft->setTextDatum(MR_DATUM);
      tft->drawString(value, x + w - 5, y + h / 2);
    }
    tft->setTextDatum(ML_DATUM);
  }

  void drawFeedNowControl(int x, int y, int w, int h, bool selected) {
      String value = String(feedAmount) + " g";
      drawButton(x, y, w, h, "FEED NOW", value.c_str(), selected, editMode);
  }

  void drawFooter() {
    int y = SCREEN_HEIGHT - STATUS_BAR_HEIGHT;
    tft->fillRect(0, y, SCREEN_WIDTH, STATUS_BAR_HEIGHT, COLOR_PANEL_BG);
    tft->setTextSize(1);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextDatum(MC_DATUM);
    if (editMode) {
        tft->drawString("Rotate: Adjust Grams | Click: Feed", SCREEN_WIDTH / 2, y + STATUS_BAR_HEIGHT / 2);
    } else {
        tft->drawString("Rotate: Select | Click: Action", SCREEN_WIDTH / 2, y + STATUS_BAR_HEIGHT / 2);
    }
  }

  void updateSensorValues() { drawSensorValues(); }
  void updateControls() { drawControls(); }
};

#endif // FEEDER_SCREEN_H