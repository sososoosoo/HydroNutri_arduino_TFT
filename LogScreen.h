#ifndef LOG_SCREEN_H
#define LOG_SCREEN_H

#include <TFT_eSPI.h>
#include "Config.h"

class LogScreen {
private:
  TFT_eSPI* tft;
  AlarmState* alarmState;
  unsigned long lastUpdate;
  bool needsFullRedraw;
  int scrollOffset;

public:
  LogScreen(TFT_eSPI* display) :
    tft(display),
    alarmState(nullptr),
    lastUpdate(0),
    needsFullRedraw(true),
    scrollOffset(0) {}

  void setStateReference(AlarmState* alarms) {
    alarmState = alarms;
  }

  void begin() {
    needsFullRedraw = true;
    scrollOffset = 0;
  }

  void update() {
    if (millis() - lastUpdate < UI_PERIOD_MS) return;
    lastUpdate = millis();

    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      if (needsFullRedraw) {
        draw();
      }
      xSemaphoreGive(g_stateMutex);
    }
  }

  void onEncoderRotate(int direction) {
    if (!alarmState || alarmState->log_count == 0) return;

    scrollOffset += direction;
    int maxOffset = alarmState->log_count - 1;
    if (scrollOffset < 0) scrollOffset = 0;
    if (scrollOffset > maxOffset) scrollOffset = maxOffset;
    
    needsFullRedraw = true;
  }

  void onButtonClick() {
    // On this screen, a click returns to the dashboard
    // This is handled in the main uiTask loop
  }

private:
  void draw() {
    tft->fillScreen(COLOR_BACKGROUND);
    drawHeader();
    drawLogList();
    drawFooter();
    needsFullRedraw = false;
  }

  void drawHeader() {
    tft->fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_HEADER_BG);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextSize(2);
    tft->setTextDatum(ML_DATUM);
    tft->drawString("ALARM LOG", 10, HEADER_HEIGHT / 2);
  }

  void drawLogList() {
    int x = 10, y = HEADER_HEIGHT + 10, lineHeight = 16;
    int max_lines_on_screen = (SCREEN_HEIGHT - HEADER_HEIGHT - STATUS_BAR_HEIGHT - 20) / lineHeight;

    tft->setTextSize(1);
    
    if (!alarmState || alarmState->log_count == 0) {
      tft->setTextColor(COLOR_TEXT);
      tft->setTextDatum(MC_DATUM);
      tft->drawString("No alarm logs.", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
      return;
    }

    for (int i = 0; i < max_lines_on_screen; ++i) {
      int log_index = (alarmState->log_head + scrollOffset + i) % MAX_ALARM_LOGS;
      if (scrollOffset + i >= alarmState->log_count) break;

      AlarmLogEntry* entry = &alarmState->log[log_index];
      
      if (entry->cleared) {
        tft->setTextColor(COLOR_OK);
      } else {
        tft->setTextColor(COLOR_ERROR);
      }
      
      tft->setCursor(x, y + i * lineHeight);
      tft->printf("%02d:%02d:%02d %s %s", entry->hour, entry->minute, entry->second, entry->cleared ? "CLEAR" : "RAISE", entry->code);
    }
  }

  void drawFooter() {
    int y = SCREEN_HEIGHT - STATUS_BAR_HEIGHT;
    tft->fillRect(0, y, SCREEN_WIDTH, STATUS_BAR_HEIGHT, COLOR_PANEL_BG);
    tft->setTextSize(1);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("Rotate: Scroll | Click: Back", SCREEN_WIDTH / 2, y + STATUS_BAR_HEIGHT / 2);
  }
};

#endif // LOG_SCREEN_H