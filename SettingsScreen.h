#ifndef SETTINGS_SCREEN_H
#define SETTINGS_SCREEN_H

#include <TFT_eSPI.h>
#include "Config.h"
#include "Persistence.h"

// 설정 항목
enum SettingsItem {
  SETTINGS_MODULE_ENABLE = 0,
  SETTINGS_FEED_SCHEDULE_1,
  SETTINGS_FEED_SCHEDULE_2,
  SETTINGS_FEED_SCHEDULE_3,
  SETTINGS_LED_BRIGHTNESS,
  SETTINGS_LED_SCHEDULE,
  SETTINGS_FW_VERSION,
  SETTINGS_FACTORY_RESET,
  SETTINGS_BACK,
  SETTINGS_COUNT
};

class SettingsScreen {
private:
  TFT_eSPI* tft;
  SystemSettings* settings;
  unsigned long lastUpdate;
  bool needsFullRedraw;

  SettingsItem selectedItem;
  bool editMode;

public:
  SettingsScreen(TFT_eSPI* display) :
    tft(display),
    settings(nullptr),
    lastUpdate(0),
    needsFullRedraw(true),
    selectedItem(SETTINGS_MODULE_ENABLE),
    editMode(false) {}

  void setSettingsReference(SystemSettings* sys_settings) {
    settings = sys_settings;
  }

  void begin() {
    selectedItem = SETTINGS_MODULE_ENABLE;
    editMode = false;
    needsFullRedraw = true;
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
    if (editMode) {
      // Handle value edits
    } else {
      int newItem = (int)selectedItem + direction;
      if (newItem < 0) newItem = SETTINGS_COUNT - 1;
      if (newItem >= SETTINGS_COUNT) newItem = 0;
      selectedItem = (SettingsItem)newItem;
    }
    needsFullRedraw = true;
  }

  void onButtonClick() {
    if (!settings) return;
    // Handle button clicks for each item
    // Example for toggling a module
    if (selectedItem == SETTINGS_MODULE_ENABLE) {
        // A sub-menu would be better here, for now just toggle tank
        settings->module_enable[0] = !settings->module_enable[0];
        saveSettings(*settings); // Save after change
    }
    needsFullRedraw = true;
  }

private:
  void draw() {
    tft->fillScreen(COLOR_BACKGROUND);
    drawHeader();
    drawSettingsList();
    drawFooter();
    needsFullRedraw = false;
  }

  void drawHeader() {
    tft->fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_HEADER_BG);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextSize(2);
    tft->setTextDatum(ML_DATUM);
    tft->drawString("SETTINGS", 10, HEADER_HEIGHT / 2);
  }

  void drawSettingsList() {
    int y = HEADER_HEIGHT + 5;
    int lineHeight = 25;
    for (int i = 0; i < SETTINGS_COUNT; i++) {
      drawSettingsItem(5, y + (i * lineHeight), (SettingsItem)i, i == selectedItem);
    }
  }

  void drawSettingsItem(int x, int y, SettingsItem item, bool selected) {
    if (selected) {
      tft->fillRect(x - 2, y - 2, SCREEN_WIDTH - 10, 23, COLOR_PANEL_BG);
      tft->drawRect(x - 2, y - 2, SCREEN_WIDTH - 10, 23, COLOR_WARNING);
    }

    tft->setTextSize(1);
    tft->setTextColor(COLOR_TEXT);
    tft->setCursor(x, y);

    switch (item) {
      case SETTINGS_MODULE_ENABLE:
        tft->print("Module Enable:");
        tft->setCursor(x + 150, y);
        tft->setTextColor(COLOR_OK);
        tft->printf("T:%d G:%d N:%d F:%d", settings->module_enable[0], settings->module_enable[1], settings->module_enable[2], settings->module_enable[3]);
        break;
      case SETTINGS_FEED_SCHEDULE_1:
        tft->print("Feed 1:");
        tft->setCursor(x + 150, y);
        tft->setTextColor(COLOR_OK);
        tft->printf("%02d:%02d %dg %s", settings->feed_schedule[0].hour, settings->feed_schedule[0].minute, settings->feed_schedule[0].grams, settings->feed_schedule[0].enabled ? "ON" : "OFF");
        break;
      // ... other cases
      default:
        tft->print("Unknown Setting");
        break;
    }
  }

  void drawFooter() {
    int y = SCREEN_HEIGHT - STATUS_BAR_HEIGHT;
    tft->fillRect(0, y, SCREEN_WIDTH, STATUS_BAR_HEIGHT, COLOR_PANEL_BG);
    tft->setTextSize(1);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("Rotate: Select | Click: Edit", SCREEN_WIDTH / 2, y + STATUS_BAR_HEIGHT / 2);
  }
};

#endif // SETTINGS_SCREEN_H
