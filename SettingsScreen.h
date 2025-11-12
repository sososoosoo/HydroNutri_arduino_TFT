#ifndef SETTINGS_SCREEN_H
#define SETTINGS_SCREEN_H

#include <TFT_eSPI.h>
#include "Config.h"
#include "Persistence.h"

// 스케줄 시간 (다른 화면과 중복되지만, 간결성을 위해 여기에 다시 정의)
struct SettingScheduleTime {
  uint8_t hour;
  uint8_t minute;
};

enum SettingsItem {
  SETTINGS_NONE = -1,
  SETTINGS_FEED1_TIME,
  SETTINGS_FEED1_AMOUNT,
  SETTINGS_FEED2_TIME,
  SETTINGS_FEED2_AMOUNT,
  SETTINGS_FEED3_TIME,
  SETTINGS_FEED3_AMOUNT,
  SETTINGS_GROW_LED_ON_TIME,
  SETTINGS_GROW_LED_OFF_TIME,
  SETTINGS_GROW_LED_BRIGHTNESS,
  SETTINGS_SAVE_AND_BACK,
  SETTINGS_ITEM_COUNT
};

class SettingsScreen {
private:
  TFT_eSPI* tft;
  SystemSettings* settings;
  unsigned long lastUpdate;
  bool needsFullRedraw;

  SettingsItem selectedItem;
  bool editMode;
  bool editingHour; // For time edits

  // UI temporary state
  SettingScheduleTime feed1_time, feed2_time, feed3_time;
  uint8_t feed1_grams, feed2_grams, feed3_grams;
  SettingScheduleTime grow_on_time, grow_off_time;
  uint8_t grow_brightness;

public:
  SettingsScreen(TFT_eSPI* display) :
    tft(display),
    settings(nullptr),
    lastUpdate(0),
    needsFullRedraw(true),
    selectedItem(SETTINGS_FEED1_TIME),
    editMode(false),
    editingHour(true) {}

  void setSettingsReference(SystemSettings* s) {
    settings = s;
  }

  void begin() {
    selectedItem = SETTINGS_FEED1_TIME;
    editMode = false;
    needsFullRedraw = true;
    copySettingsToLocal();
  }

  void update() {
    if (millis() - lastUpdate < UI_PERIOD_MS) return;
    lastUpdate = millis();
    if (needsFullRedraw) {
      draw();
    } else {
      updateControls();
    }
  }

  void onEncoderRotate(int direction) {
    if (editMode) {
      handleEdit(direction);
    } else {
      int newItem = (int)selectedItem + direction;
      if (newItem < 0) newItem = SETTINGS_ITEM_COUNT - 1;
      if (newItem >= SETTINGS_ITEM_COUNT) newItem = 0;
      selectedItem = (SettingsItem)newItem;
    }
    needsFullRedraw = true;
  }

  void onButtonClick() {
    if (selectedItem == SETTINGS_SAVE_AND_BACK) {
      if (editMode) { // Should not be in edit mode here, but as a safeguard
        editMode = false;
      } else {
        saveLocalToSettings();
      }
      return; // Return to dashboard is handled by uiTask
    }

    // For time items, click toggles between hour and minute
    if (isTimeItem(selectedItem)) {
        if (editMode) {
            editingHour = !editingHour;
        } else {
            editMode = true;
            editingHour = true;
        }
    } else { // For amount/brightness items
        editMode = !editMode;
    }
    needsFullRedraw = true;
  }

  SettingsItem getSelectedItem() const { return selectedItem; }

private:
  void copySettingsToLocal() {
    if (!settings) return;
    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      feed1_time = {settings->feed_schedule[0].hour, settings->feed_schedule[0].minute};
      feed1_grams = settings->feed_schedule[0].grams;
      feed2_time = {settings->feed_schedule[1].hour, settings->feed_schedule[1].minute};
      feed2_grams = settings->feed_schedule[1].grams;
      feed3_time = {settings->feed_schedule[2].hour, settings->feed_schedule[2].minute};
      feed3_grams = settings->feed_schedule[2].grams;
      grow_on_time = {settings->grow_led_schedule.on_hour, settings->grow_led_schedule.on_minute};
      grow_off_time = {settings->grow_led_schedule.off_hour, settings->grow_led_schedule.off_minute};
      grow_brightness = settings->grow_led_brightness;
      xSemaphoreGive(g_stateMutex);
    }
  }

  void saveLocalToSettings() {
    if (!settings) return;
    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      settings->feed_schedule[0] = {feed1_time.hour, feed1_time.minute, feed1_grams, settings->feed_schedule[0].enabled};
      settings->feed_schedule[1] = {feed2_time.hour, feed2_time.minute, feed2_grams, settings->feed_schedule[1].enabled};
      settings->feed_schedule[2] = {feed3_time.hour, feed3_time.minute, feed3_grams, settings->feed_schedule[2].enabled};
      settings->grow_led_schedule = {grow_on_time.hour, grow_on_time.minute, grow_off_time.hour, grow_off_time.minute, settings->grow_led_schedule.enabled};
      settings->grow_led_brightness = grow_brightness;
      
      saveSettings(*settings); // Persist to NVS
      Serial.println("Settings saved to NVS.");

      xSemaphoreGive(g_stateMutex);
    } else {
      Serial.println("SettingsScreen: Failed to get mutex for saving.");
    }
  }

  bool isTimeItem(SettingsItem item) {
    return item == SETTINGS_FEED1_TIME || item == SETTINGS_FEED2_TIME || item == SETTINGS_FEED3_TIME ||
           item == SETTINGS_GROW_LED_ON_TIME || item == SETTINGS_GROW_LED_OFF_TIME;
  }

  void handleEdit(int direction) {
    switch(selectedItem) {
      case SETTINGS_FEED1_TIME: adjustTime(feed1_time, direction); break;
      case SETTINGS_FEED1_AMOUNT: adjustValue(feed1_grams, direction, 1, 50); break;
      case SETTINGS_FEED2_TIME: adjustTime(feed2_time, direction); break;
      case SETTINGS_FEED2_AMOUNT: adjustValue(feed2_grams, direction, 1, 50); break;
      case SETTINGS_FEED3_TIME: adjustTime(feed3_time, direction); break;
      case SETTINGS_FEED3_AMOUNT: adjustValue(feed3_grams, direction, 1, 50); break;
      case SETTINGS_GROW_LED_ON_TIME: adjustTime(grow_on_time, direction); break;
      case SETTINGS_GROW_LED_OFF_TIME: adjustTime(grow_off_time, direction); break;
      case SETTINGS_GROW_LED_BRIGHTNESS: adjustValue(grow_brightness, direction * 5, 0, 100); break;
      default: break;
    }
  }

  void adjustTime(SettingScheduleTime& time, int direction) {
    if (editingHour) time.hour = (time.hour + direction + 24) % 24;
    else time.minute = (time.minute + direction + 60) % 60;
  }

  void adjustValue(uint8_t& value, int direction, int min, int max) {
    int newValue = value + direction;
    if (newValue < min) newValue = min;
    if (newValue > max) newValue = max;
    value = newValue;
  }

  void draw() {
    tft->fillScreen(COLOR_BACKGROUND);
    drawHeader();
    drawControls();
    drawFooter();
    needsFullRedraw = false;
  }

  void drawHeader() {
    tft->fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_HEADER_BG);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextSize(2);
    tft->setTextDatum(ML_DATUM);
    tft->drawString("SYSTEM SETTINGS", 10, HEADER_HEIGHT / 2);
  }

  void drawControls() {
    int x = 10, y = HEADER_HEIGHT + 10, w = 145, h = 28, spacing = 5;
    
    // Feeder Settings
    drawTimeAmountRow(x, y, w, h, "Feed 1", feed1_time, feed1_grams, "g", SETTINGS_FEED1_TIME, SETTINGS_FEED1_AMOUNT);
    y += h + spacing;
    drawTimeAmountRow(x, y, w, h, "Feed 2", feed2_time, feed2_grams, "g", SETTINGS_FEED2_TIME, SETTINGS_FEED2_AMOUNT);
    y += h + spacing;
    drawTimeAmountRow(x, y, w, h, "Feed 3", feed3_time, feed3_grams, "g", SETTINGS_FEED3_TIME, SETTINGS_FEED3_AMOUNT);
    y += h + spacing + 10;

    // GrowBox Settings
    drawTimeAmountRow(x, y, w, h, "LED On", grow_on_time, grow_brightness, "%", SETTINGS_GROW_LED_ON_TIME, SETTINGS_GROW_LED_BRIGHTNESS);
    y += h + spacing;
    drawTimeAmountRow(x, y, w, h, "LED Off", grow_off_time, 0, "", SETTINGS_GROW_LED_OFF_TIME, SETTINGS_NONE);
    y += h + spacing + 15;

    // Save Button
    uint16_t borderColor = (selectedItem == SETTINGS_SAVE_AND_BACK) ? COLOR_WARNING : COLOR_TEXT;
    tft->fillRect(85, y, 150, 35, COLOR_PANEL_BG);
    tft->drawRect(85, y, 150, 35, borderColor);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextSize(2);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("SAVE & BACK", 160, y + 18);
  }

  void drawTimeAmountRow(int x, int y, int w, int h, const char* label, SettingScheduleTime time, uint8_t amount, const char* unit, SettingsItem timeItem, SettingsItem amountItem) {
    tft->setTextSize(1);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextDatum(ML_DATUM);
    tft->drawString(label, x, y + h/2);

    // Time Box
    char timeStr[6];
    sprintf(timeStr, "%02d:%02d", time.hour, time.minute);
    drawValueBox(x + 60, y, w, h, timeStr, timeItem);

    // Amount Box
    if (amountItem != SETTINGS_NONE) {
      char amountStr[5];
      sprintf(amountStr, "%d%s", amount, unit);
      drawValueBox(x + 60 + w + 5, y, w/2, h, amountStr, amountItem);
    }
  }

  void drawValueBox(int x, int y, int w, int h, const char* value, SettingsItem item) {
    bool selected = (selectedItem == item);
    uint16_t borderColor = selected ? COLOR_WARNING : COLOR_TEXT;
    tft->fillRect(x, y, w, h, COLOR_PANEL_BG);
    tft->drawRect(x, y, w, h, borderColor);
    if (selected) tft->drawRect(x+1, y+1, w-2, h-2, borderColor);

    tft->setTextSize(2);
    tft->setTextDatum(MC_DATUM);

    if (editMode && selected && isTimeItem(item)) {
        tft->setCursor(x + w/2 - 22, y + h/2 + 3);
        if (editingHour) tft->setTextColor(COLOR_WARNING); else tft->setTextColor(COLOR_OK);
        tft->printf("%02d", ((SettingScheduleTime*) &feed1_time)[item].hour); // Risky cast, assumes layout
        tft->setTextColor(COLOR_TEXT);
        tft->print(":");
        if (!editingHour) tft->setTextColor(COLOR_WARNING); else tft->setTextColor(COLOR_OK);
        tft->printf("%02d", ((SettingScheduleTime*) &feed1_time)[item].minute);
    } else {
        tft->setTextColor((editMode && selected) ? COLOR_WARNING : COLOR_OK);
        tft->drawString(value, x + w/2, y + h/2 + 3);
    }
  }

  void drawFooter() {
    int y = SCREEN_HEIGHT - STATUS_BAR_HEIGHT;
    tft->fillRect(0, y, SCREEN_WIDTH, STATUS_BAR_HEIGHT, COLOR_PANEL_BG);
    tft->setTextSize(1);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextDatum(MC_DATUM);
    if (editMode) {
      if (isTimeItem(selectedItem)) {
        tft->drawString("Rotate: Adjust | Click: H/M", SCREEN_WIDTH / 2, y + STATUS_BAR_HEIGHT / 2);
      } else {
        tft->drawString("Rotate: Adjust | Click: Save", SCREEN_WIDTH / 2, y + STATUS_BAR_HEIGHT / 2);
      }
    } else {
      tft->drawString("Rotate: Select | Click: Edit", SCREEN_WIDTH / 2, y + STATUS_BAR_HEIGHT / 2);
    }
  }

  void updateControls() {
    drawControls();
    drawFooter();
  }
};

#endif // SETTINGS_SCREEN_H