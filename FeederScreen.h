#ifndef FEEDER_SCREEN_H
#define FEEDER_SCREEN_H

#include <TFT_eSPI.h>
#include "Config.h"

// 제어 모드
enum FeederControlItem {
  FEEDER_CONTROL_NONE = -1,
  FEEDER_CONTROL_FEED_NOW = 0,
  FEEDER_CONTROL_SCHEDULE_ENABLE,
  FEEDER_CONTROL_SCHEDULE_TIME1,
  FEEDER_CONTROL_SCHEDULE_TIME2,
  FEEDER_CONTROL_SCHEDULE_TIME3,
  FEEDER_CONTROL_BACK,
  FEEDER_CONTROL_COUNT
};

// 스케줄 시간 (급여기용)
struct FeedScheduleTime {
  uint8_t hour;
  uint8_t minute;
  bool enabled;
};

class FeederScreen {
private:
  TFT_eSPI* tft;
  SystemState* systemState;
  unsigned long lastUpdate;
  bool needsFullRedraw;

  // 제어 상태
  FeederControlItem selectedItem;
  bool editMode;
  bool editingHour;

  // UI용 임시 상태
  FeedScheduleTime scheduleTime1, scheduleTime2, scheduleTime3;
  bool scheduleActive;
  bool feedingNow;

public:
  FeederScreen(TFT_eSPI* display) :
    tft(display),
    systemState(nullptr),
    lastUpdate(0),
    needsFullRedraw(true),
    selectedItem(FEEDER_CONTROL_NONE),
    editMode(false),
    editingHour(true),
    scheduleActive(false),
    feedingNow(false) {
    scheduleTime1 = {8, 0, true};
    scheduleTime2 = {12, 0, true};
    scheduleTime3 = {18, 0, true};
  }

  void setStateReference(SystemState* state) {
    systemState = state;
  }

  void begin() {
    selectedItem = FEEDER_CONTROL_FEED_NOW;
    editMode = false;
    editingHour = true;
    feedingNow = false;
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
            updateStatusInfo();
            updateControls();
        }
        xSemaphoreGive(g_stateMutex);
    } else {
        Serial.println("FeederScreen: Failed to get state mutex.");
    }
  }

  void forceRedraw() {
    needsFullRedraw = true;
  }

  void onEncoderRotate(int direction) {
    if (editMode) {
      if (selectedItem == FEEDER_CONTROL_SCHEDULE_TIME1) adjustScheduleTime(scheduleTime1, direction);
      else if (selectedItem == FEEDER_CONTROL_SCHEDULE_TIME2) adjustScheduleTime(scheduleTime2, direction);
      else if (selectedItem == FEEDER_CONTROL_SCHEDULE_TIME3) adjustScheduleTime(scheduleTime3, direction);
    } else {
      int newItem = (int)selectedItem + direction;
      if (newItem < 0) newItem = FEEDER_CONTROL_COUNT - 1;
      if (newItem >= FEEDER_CONTROL_COUNT) newItem = 0;
      selectedItem = (FeederControlItem)newItem;
    }
    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        updateControls();
        xSemaphoreGive(g_stateMutex);
    }
  }

  void onButtonClick() {
    if (selectedItem == FEEDER_CONTROL_BACK) {
      if (editMode) editMode = false;
      return;
    }
    if (selectedItem == FEEDER_CONTROL_FEED_NOW) {
      feedingNow = true; /* TODO: Send CAN command */
    }
    if (selectedItem == FEEDER_CONTROL_SCHEDULE_ENABLE) {
      scheduleActive = !scheduleActive; /* TODO: Send CAN command */
    }
    if (selectedItem >= FEEDER_CONTROL_SCHEDULE_TIME1 && selectedItem <= FEEDER_CONTROL_SCHEDULE_TIME3) {
      if (editMode) editingHour = !editingHour;
      else { editMode = true; editingHour = true; }
    }
    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        updateControls();
        xSemaphoreGive(g_stateMutex);
    }
  }

private:
  void adjustScheduleTime(FeedScheduleTime& time, int direction) {
    if (editingHour) time.hour = (time.hour + direction + 24) % 24;
    else time.minute = (time.minute + direction * 5 + 60) % 60;
  }

  void draw() {
    tft->fillScreen(COLOR_BACKGROUND);
    drawHeader();
    drawStatusInfo();
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

  void drawStatusInfo() {
    int x = 10;
    int y = HEADER_HEIGHT + 10;
    int lineHeight = 18;

    tft->setTextSize(1);
    tft->setTextColor(0x07FF);
    tft->setCursor(x, y);
    tft->print("=== STATUS INFO ===");
    y += lineHeight + 5;

    if (systemState && systemState->comm.feed.ok) {
      tft->setTextColor(COLOR_TEXT);
      tft->setCursor(x, y);
      tft->print("Food Level:");
      tft->setCursor(x + 100, y);
      tft->setTextColor(systemState->feed.remain_g > 200 ? COLOR_OK : COLOR_WARNING);
      tft->printf("%d g", systemState->feed.remain_g);
      y += lineHeight;

      int barX = x;
      int barY = y;
      int barW = 150;
      int barH = 10;
      tft->drawRect(barX, barY, barW, barH, COLOR_TEXT);
      int fillW = (barW - 2) * systemState->feed.remain_g / 1000;
      if(fillW < 0) fillW = 0;
      if(fillW > barW - 2) fillW = barW - 2;
      uint16_t barColor = systemState->feed.remain_g > 200 ? COLOR_OK : COLOR_WARNING;
      tft->fillRect(barX + 1, barY + 1, fillW, barH - 2, barColor);
      y += lineHeight + 5;

      if (systemState->feed.remain_g < 200) {
        tft->setTextColor(COLOR_WARNING);
        tft->setCursor(x, y);
        tft->print("! LOW FOOD !");
        y += lineHeight;
      }
    } else {
      tft->setTextColor(COLOR_ERROR);
      tft->setTextDatum(MC_DATUM);
      tft->drawString("No Data Available", x + 80, y + 40);
    }
  }

  void drawControls() {
    int x = 170;
    int y = HEADER_HEIGHT + 10;
    int buttonHeight = 28;

    tft->setTextSize(1);
    tft->setTextColor(0x07FF);
    tft->setCursor(x, y);
    tft->print("=== CONTROLS ===");
    y += 23;

    drawButton(x, y, 140, buttonHeight + 10, "FEED NOW", feedingNow ? "FEEDING..." : "", selectedItem == FEEDER_CONTROL_FEED_NOW, feedingNow ? COLOR_OK : COLOR_TEXT);
    y += buttonHeight + 15;

    drawButton(x, y, 140, buttonHeight, "SCHEDULE", scheduleActive ? "ON" : "OFF", selectedItem == FEEDER_CONTROL_SCHEDULE_ENABLE, scheduleActive ? COLOR_OK : COLOR_INACTIVE);
    y += buttonHeight + 5;

    drawScheduleTime(x, y, 140, buttonHeight, "Time 1", scheduleTime1, selectedItem == FEEDER_CONTROL_SCHEDULE_TIME1);
    y += buttonHeight + 5;

    drawScheduleTime(x, y, 140, buttonHeight, "Time 2", scheduleTime2, selectedItem == FEEDER_CONTROL_SCHEDULE_TIME2);
    y += buttonHeight + 5;

    drawScheduleTime(x, y, 140, buttonHeight, "Time 3", scheduleTime3, selectedItem == FEEDER_CONTROL_SCHEDULE_TIME3);
    y += buttonHeight + 5;

    drawButton(x, y, 140, buttonHeight, "BACK", "", selectedItem == FEEDER_CONTROL_BACK, COLOR_INACTIVE);
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
      tft->drawString(value, x + 5, y + 22);
    }
  }

  void drawScheduleTime(int x, int y, int w, int h, const char* label, const FeedScheduleTime& time, bool selected) {
    uint16_t borderColor = selected ? COLOR_WARNING : COLOR_TEXT;
    tft->fillRect(x, y, w, h, COLOR_PANEL_BG);
    tft->drawRect(x, y, w, h, borderColor);
    if (selected) tft->drawRect(x + 1, y + 1, w - 2, h - 2, borderColor);

    tft->setTextColor(COLOR_TEXT);
    tft->setTextSize(1);
    tft->setTextDatum(ML_DATUM);
    tft->drawString(label, x + 5, y + 8);

    tft->setTextSize(2);
    tft->setTextDatum(ML_DATUM);
    tft->setCursor(x + 5, y + 15);
    if (editMode && selected) {
        if (editingHour) tft->setTextColor(COLOR_WARNING); else tft->setTextColor(COLOR_OK);
        tft->printf("%02d", time.hour);
        tft->setTextColor(COLOR_TEXT);
        tft->print(":");
        if (!editingHour) tft->setTextColor(COLOR_WARNING); else tft->setTextColor(COLOR_OK);
        tft->printf("%02d", time.minute);
    } else {
        tft->setTextColor(COLOR_OK);
        tft->printf("%02d:%02d", time.hour, time.minute);
    }
  }

  void drawFooter() {
    int y = SCREEN_HEIGHT - STATUS_BAR_HEIGHT;
    tft->fillRect(0, y, SCREEN_WIDTH, STATUS_BAR_HEIGHT, COLOR_PANEL_BG);
    tft->setTextSize(1);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextDatum(MC_DATUM);
    if (editMode) {
      tft->drawString("Rotate: Adjust | Click: H/M", SCREEN_WIDTH / 2, y + STATUS_BAR_HEIGHT / 2);
    } else {
      tft->drawString("Rotate: Select | Click: Action", SCREEN_WIDTH / 2, y + STATUS_BAR_HEIGHT / 2);
    }
  }

  void updateStatusInfo() { drawStatusInfo(); }
  void updateControls() { drawControls(); drawFooter(); }
};

#endif // FEEDER_SCREEN_H
