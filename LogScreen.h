#ifndef LOG_SCREEN_H
#define LOG_SCREEN_H

#include <TFT_eSPI.h>
#include "Config.h"
#include "AlarmManager.h"

class LogScreen {
private:
  TFT_eSPI* tft;
  AlarmState* alarmState;
  unsigned long lastUpdate;
  bool needsFullRedraw;

  int scrollPosition;
  bool showingMenu;
  enum MenuOption { MENU_CLEAR_ALL, MENU_BACK, MENU_COUNT };
  MenuOption selectedMenu;

  const int LOGS_PER_PAGE = 8;

public:
  LogScreen(TFT_eSPI* display) :
    tft(display),
    alarmState(nullptr),
    lastUpdate(0),
    needsFullRedraw(true),
    scrollPosition(0),
    showingMenu(false),
    selectedMenu(MENU_CLEAR_ALL) {}

  void setStateReference(AlarmState* alarms) {
    alarmState = alarms;
  }

  void begin() {
    scrollPosition = 0;
    showingMenu = false;
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
        }
        xSemaphoreGive(g_stateMutex);
    } else {
        Serial.println("LogScreen: Failed to get state mutex.");
    }
  }

  void onEncoderRotate(int direction) {
    if (showingMenu) {
      int newMenu = (int)selectedMenu + direction;
      if (newMenu < 0) newMenu = MENU_COUNT - 1;
      if (newMenu >= MENU_COUNT) newMenu = 0;
      selectedMenu = (MenuOption)newMenu;
    } else {
      if (alarmState && alarmState->count > 0) {
        scrollPosition += direction;
        if (scrollPosition < 0) scrollPosition = 0;
        if (scrollPosition >= alarmState->count) scrollPosition = alarmState->count - 1;
      }
    }
    needsFullRedraw = true;
  }

  void onButtonClick() {
    if (showingMenu) {
      if (selectedMenu == MENU_CLEAR_ALL) {
        if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            clear_all_alarms();
            xSemaphoreGive(g_stateMutex);
        }
        showingMenu = false;
      } else if (selectedMenu == MENU_BACK) {
        showingMenu = false;
      }
    } else {
      showingMenu = true;
      selectedMenu = MENU_CLEAR_ALL;
    }
    needsFullRedraw = true;
  }

private:
  void draw() {
    tft->fillScreen(COLOR_BACKGROUND);
    drawHeader();
    if (showingMenu) {
      drawMenu();
    } else {
      drawLogList();
    }
    drawFooter();
    needsFullRedraw = false;
  }

  void drawHeader() {
    tft->fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_HEADER_BG);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextSize(2);
    tft->setTextDatum(ML_DATUM);
    tft->drawString("LOGS & ALERTS", 10, HEADER_HEIGHT / 2);
  }

  void drawLogList() {
    int y = HEADER_HEIGHT + 5;
    int lineHeight = 22;

    if (!alarmState || alarmState->count == 0) {
      tft->setTextColor(COLOR_INACTIVE);
      tft->setTextDatum(MC_DATUM);
      tft->drawString("No active alarms", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
      return;
    }

    int startIdx = scrollPosition;
    for (int i = 0; i < LOGS_PER_PAGE; ++i) {
        int logIdx = startIdx + i;
        if (logIdx >= alarmState->count) break;
        drawLogEntry(5, y + (i * lineHeight), alarmState->active_alarms[logIdx], logIdx == scrollPosition);
    }
  }

  void drawLogEntry(int x, int y, const Alarm& alarm, bool selected) {
    if (selected) {
      tft->fillRect(x - 2, y - 2, SCREEN_WIDTH - 10, 20, COLOR_PANEL_BG);
      tft->drawRect(x - 2, y - 2, SCREEN_WIDTH - 10, 20, COLOR_WARNING);
    }

    uint16_t typeColor = (alarm.code[0] == 'E') ? COLOR_ERROR : COLOR_WARNING;
    const char* typeIcon = (alarm.code[0] == 'E') ? "[E]" : "[W]";

    tft->setTextSize(1);
    tft->setTextColor(typeColor);
    tft->setCursor(x, y);
    tft->print(typeIcon);

    tft->setTextColor(COLOR_TEXT);
    tft->setCursor(x + 25, y);
    tft->print(alarm.msg);

    tft->setTextColor(COLOR_INACTIVE);
    unsigned long timeSince = (millis() - alarm.raised_at_ms) / 1000; // seconds
    tft->setTextDatum(MR_DATUM);
    tft->drawString(String(timeSince) + "s ago", SCREEN_WIDTH - 10, y + 4);
    tft->setTextDatum(ML_DATUM);
  }

  void drawMenu() {
    // Simplified menu
    int menuWidth = 160, menuHeight = 80;
    int menuX = (SCREEN_WIDTH - menuWidth) / 2;
    int menuY = (SCREEN_HEIGHT - menuHeight) / 2;

    tft->fillRect(menuX, menuY, menuWidth, menuHeight, COLOR_PANEL_BG);
    tft->drawRect(menuX, menuY, menuWidth, menuHeight, COLOR_TEXT);

    drawMenuItem(menuX + 10, menuY + 10, menuWidth - 20, 25, "Clear All", selectedMenu == MENU_CLEAR_ALL);
    drawMenuItem(menuX + 10, menuY + 45, menuWidth - 20, 25, "Back", selectedMenu == MENU_BACK);
  }

  void drawMenuItem(int x, int y, int w, int h, const char* label, bool selected) {
    if (selected) {
      tft->fillRect(x, y, w, h, 0x18E3);
      tft->drawRect(x, y, w, h, COLOR_WARNING);
    } else {
      tft->drawRect(x, y, w, h, COLOR_TEXT);
    }
    tft->setTextColor(COLOR_TEXT);
    tft->setTextSize(1);
    tft->setTextDatum(MC_DATUM);
    tft->drawString(label, x + w / 2, y + h / 2);
    tft->setTextDatum(ML_DATUM);
  }

  void drawFooter() {
    int y = SCREEN_HEIGHT - STATUS_BAR_HEIGHT;
    tft->fillRect(0, y, SCREEN_WIDTH, STATUS_BAR_HEIGHT, COLOR_PANEL_BG);
    tft->setTextSize(1);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextDatum(MC_DATUM);
    if (showingMenu) {
      tft->drawString("Rotate: Select | Click: Apply", SCREEN_WIDTH / 2, y + STATUS_BAR_HEIGHT / 2);
    } else {
      tft->drawString("Rotate: Scroll | Click: Menu", SCREEN_WIDTH / 2, y + STATUS_BAR_HEIGHT / 2);
    }
  }
};

#endif // LOG_SCREEN_H
