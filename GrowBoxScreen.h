#ifndef GROWBOX_SCREEN_H
#define GROWBOX_SCREEN_H

#include <TFT_eSPI.h>
#include "Config.h"
#include "CANHandler.h"

// 제어 모드
enum GrowBoxControlItem {
  GROWBOX_CONTROL_NONE = -1,
  GROWBOX_CONTROL_LED_BRIGHTNESS = 0,
  GROWBOX_CONTROL_SCHEDULE_ENABLE,
  GROWBOX_CONTROL_SCHEDULE_ON_TIME,
  GROWBOX_CONTROL_SCHEDULE_OFF_TIME,
  GROWBOX_CONTROL_BACK,
  GROWBOX_CONTROL_COUNT
};

// 스케줄 시간
struct ScheduleTime {
  uint8_t hour;
  uint8_t minute;
};

class GrowBoxScreen {
private:
  TFT_eSPI* tft;
  SystemState* systemState;
  CANHandler* canHandler;
  unsigned long lastUpdate;
  bool needsFullRedraw;

  // 제어 상태
  GrowBoxControlItem selectedItem;
  bool editMode;
  bool editingHour;

  // UI용 임시 상태
  ScheduleTime scheduleOnTime;
  ScheduleTime scheduleOffTime;
  bool scheduleEnable;
  int ledBrightness;

public:
  GrowBoxScreen(TFT_eSPI* display) :
    tft(display),
    systemState(nullptr),
    canHandler(nullptr),
    lastUpdate(0),
    needsFullRedraw(true),
    selectedItem(GROWBOX_CONTROL_LED_BRIGHTNESS),
    editMode(false),
    editingHour(true) {
    scheduleOnTime = {8, 0};
    scheduleOffTime = {20, 0};
    scheduleEnable = false;
    ledBrightness = 0;
  }

  void setStateReference(SystemState* state) {
    systemState = state;
  }

  void setCANHandler(CANHandler* handler) {
    canHandler = handler;
  }

  void begin() {
    selectedItem = GROWBOX_CONTROL_LED_BRIGHTNESS;
    editMode = false;
    editingHour = true;
    needsFullRedraw = true;

    if (systemState && xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        ledBrightness = systemState->grow.led;
        // scheduleEnable = g_settings.grow_led_schedule.enabled;
        // scheduleOnTime = {g_settings.grow_led_schedule.on_hour, g_settings.grow_led_schedule.on_minute};
        // scheduleOffTime = {g_settings.grow_led_schedule.off_hour, g_settings.grow_led_schedule.off_minute};
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
        Serial.println("GrowBoxScreen: Failed to get state mutex.");
    }
  }

  void onEncoderRotate(int direction) {
    if (editMode) {
      if (selectedItem == GROWBOX_CONTROL_LED_BRIGHTNESS) {
        ledBrightness += (direction * 5);
        if (ledBrightness < 0) ledBrightness = 0;
        if (ledBrightness > 100) ledBrightness = 100;
      } else if (selectedItem == GROWBOX_CONTROL_SCHEDULE_ON_TIME) {
        adjustScheduleTime(scheduleOnTime, direction);
      } else if (selectedItem == GROWBOX_CONTROL_SCHEDULE_OFF_TIME) {
        adjustScheduleTime(scheduleOffTime, direction);
      }
    } else {
      int newItem = (int)selectedItem + direction;
      if (newItem < 0) newItem = GROWBOX_CONTROL_COUNT - 1;
      if (newItem >= GROWBOX_CONTROL_COUNT) newItem = 0;
      selectedItem = (GrowBoxControlItem)newItem;
    }
    needsFullRedraw = true;
  }

  void onButtonClick() {
    if (selectedItem == GROWBOX_CONTROL_BACK) {
      if (editMode) editMode = false;
      return;
    }
    if (!canHandler) return;

    if (selectedItem == GROWBOX_CONTROL_LED_BRIGHTNESS) {
      editMode = !editMode;
      if (!editMode) {
        canHandler->setGrowBoxLED(ledBrightness);
        Serial.printf("CAN CMD: Set Grow LED to %d\n", ledBrightness);
      }
    }
    if (selectedItem == GROWBOX_CONTROL_SCHEDULE_ENABLE) {
      scheduleEnable = !scheduleEnable;
      canHandler->setGrowBoxSchedule(scheduleEnable);
      Serial.printf("CAN CMD: Set Grow Schedule %s\n", scheduleEnable ? "ON" : "OFF");
    }
    if (selectedItem == GROWBOX_CONTROL_SCHEDULE_ON_TIME || selectedItem == GROWBOX_CONTROL_SCHEDULE_OFF_TIME) {
      if (editMode) {
        editingHour = !editingHour;
      } else {
        editMode = true;
        editingHour = true;
      }
      if (!editMode) {
        // TODO: Send schedule time command
      }
    }
    needsFullRedraw = true;
  }

  GrowBoxControlItem getSelectedItem() const { return selectedItem; }
  bool isEditMode() const { return editMode; }

private:
  void adjustScheduleTime(ScheduleTime& time, int direction) {
    if (editingHour) time.hour = (time.hour + direction + 24) % 24;
    else time.minute = (time.minute + direction * 5 + 60) % 60;
  }

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
    tft->drawString("GROWBOX DETAIL", 10, HEADER_HEIGHT / 2);
  }

  void drawSensorValues() {
    int x = 10, y = HEADER_HEIGHT + 10, lineHeight = 18;
    tft->setTextSize(1);
    tft->setTextColor(0x07FF);
    tft->setCursor(x, y);
    tft->print("=== SENSOR DATA ===");
    y += lineHeight + 5;

    if (systemState && systemState->comm.grow.ok) {
      tft->setTextColor(COLOR_TEXT);
      tft->setCursor(x, y); tft->printf("Temperature: %.1f C", systemState->grow.temp); y += lineHeight;
      tft->setCursor(x, y); tft->printf("Humidity: %.0f %%", systemState->grow.hum); y += lineHeight;
      tft->setCursor(x, y); tft->print("Leak Status:");
      tft->setCursor(x + 100, y);
      if (systemState->grow.leak_bits > 0) {
        tft->setTextColor(COLOR_ERROR);
        tft->print("DETECTED!");
      } else {
        tft->setTextColor(COLOR_OK);
        tft->print("OK");
      }
      y += lineHeight;
      tft->setTextColor(COLOR_TEXT);
      tft->setCursor(x, y); tft->print("LED Status:");
      tft->setCursor(x + 100, y); tft->setTextColor(COLOR_OK);
      tft->printf("%d%%", systemState->grow.led);
    } else {
      tft->setTextColor(COLOR_ERROR);
      tft->setTextDatum(MC_DATUM);
      tft->drawString("No Data Available", x + 80, y + 40);
      tft->setTextDatum(ML_DATUM);
    }
  }

  void drawControls() {
    int x = 170, y = HEADER_HEIGHT + 10, buttonHeight = 28;
    tft->setTextSize(1);
    tft->setTextColor(0x07FF);
    tft->setCursor(x, y);
    tft->print("=== CONTROLS ===");
    y += 23;

    drawBrightnessControl(x, y, 140, buttonHeight + 20, selectedItem == GROWBOX_CONTROL_LED_BRIGHTNESS);
    y += buttonHeight + 25;
    drawButton(x, y, 140, buttonHeight, "SCHEDULE", scheduleEnable ? "ON" : "OFF", selectedItem == GROWBOX_CONTROL_SCHEDULE_ENABLE, scheduleEnable ? COLOR_OK : COLOR_INACTIVE);
    y += buttonHeight + 5;
    drawScheduleTime(x, y, 140, buttonHeight, "ON TIME", scheduleOnTime, selectedItem == GROWBOX_CONTROL_SCHEDULE_ON_TIME);
    y += buttonHeight + 5;
    drawScheduleTime(x, y, 140, buttonHeight, "OFF TIME", scheduleOffTime, selectedItem == GROWBOX_CONTROL_SCHEDULE_OFF_TIME);
    y += buttonHeight + 5;
    drawButton(x, y, 140, buttonHeight, "BACK", "", selectedItem == GROWBOX_CONTROL_BACK, COLOR_INACTIVE);
  }

  void drawButton(int x, int y, int w, int h, const char* label, const char* value, bool selected, uint16_t valueColor) {
    uint16_t borderColor = selected ? COLOR_WARNING : COLOR_TEXT;
    tft->fillRect(x, y, w, h, COLOR_PANEL_BG);
    tft->drawRect(x, y, w, h, borderColor);
    if (selected) tft->drawRect(x + 1, y + 1, w - 2, h - 2, borderColor);
    tft->setTextColor(COLOR_TEXT, COLOR_PANEL_BG);
    tft->setTextSize(1);
    tft->setTextDatum(ML_DATUM);
    tft->drawString(label, x + 5, y + 8);
    if (strlen(value) > 0) {
      tft->setTextColor(valueColor, COLOR_PANEL_BG);
      tft->setTextSize(2);
      tft->drawString(value, x + 5, y + 20);
    }
  }

  void drawBrightnessControl(int x, int y, int w, int h, bool selected) {
    uint16_t borderColor = selected ? COLOR_WARNING : COLOR_TEXT;
    tft->fillRect(x, y, w, h, COLOR_PANEL_BG);
    tft->drawRect(x, y, w, h, borderColor);
    if (selected) tft->drawRect(x + 1, y + 1, w - 2, h - 2, borderColor);
    tft->setTextColor(COLOR_TEXT, COLOR_PANEL_BG);
    tft->setTextSize(1);
    tft->setTextDatum(ML_DATUM);
    tft->drawString("LED BRIGHTNESS", x + 5, y + 8);
    tft->setTextColor(COLOR_OK, COLOR_PANEL_BG);
    tft->setTextSize(2);
    tft->drawString(String(ledBrightness) + "%", x + 5, y + 25);
    if (editMode && selected) {
      tft->setTextColor(COLOR_WARNING, COLOR_PANEL_BG);
      tft->setTextSize(1);
      tft->setTextDatum(MR_DATUM);
      tft->drawString("EDIT", x + w - 5, y + 25);
    }
    int barX = x + 5, barY = y + h - 8, barW = w - 10, barH = 5;
    tft->drawRect(barX, barY, barW, barH, COLOR_TEXT);
    int fillW = (barW - 2) * ledBrightness / 100;
    tft->fillRect(barX + 1, barY + 1, fillW, barH - 2, COLOR_OK);
    tft->setTextDatum(ML_DATUM);
  }

  void drawScheduleTime(int x, int y, int w, int h, const char* label, const ScheduleTime& time, bool selected) {
    uint16_t borderColor = selected ? COLOR_WARNING : COLOR_TEXT;
    tft->fillRect(x, y, w, h, COLOR_PANEL_BG);
    tft->drawRect(x, y, w, h, borderColor);
    if (selected) tft->drawRect(x + 1, y + 1, w - 2, h - 2, borderColor);
    tft->setTextColor(COLOR_TEXT, COLOR_PANEL_BG);
    tft->setTextSize(1);
    tft->setTextDatum(ML_DATUM);
    tft->drawString(label, x + 5, y + 8);
    tft->setTextSize(2);
    tft->setCursor(x + 5, y + 15);
    if (editMode && selected) {
        if (editingHour) tft->setTextColor(COLOR_WARNING, COLOR_PANEL_BG); else tft->setTextColor(COLOR_OK, COLOR_PANEL_BG);
        tft->printf("%02d", time.hour);
        tft->setTextColor(COLOR_TEXT, COLOR_PANEL_BG);
        tft->print(":");
        if (!editingHour) tft->setTextColor(COLOR_WARNING, COLOR_PANEL_BG); else tft->setTextColor(COLOR_OK, COLOR_PANEL_BG);
        tft->printf("%02d", time.minute);
    } else {
        tft->setTextColor(COLOR_OK, COLOR_PANEL_BG);
        tft->printf("%02d:%02d", time.hour, time.minute);
    }
    tft->setTextDatum(ML_DATUM);
  }

  void drawFooter() {
    int y = SCREEN_HEIGHT - STATUS_BAR_HEIGHT;
    tft->fillRect(0, y, SCREEN_WIDTH, STATUS_BAR_HEIGHT, COLOR_PANEL_BG);
    tft->setTextSize(1);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextDatum(MC_DATUM);
    if (editMode) {
      if (selectedItem == GROWBOX_CONTROL_SCHEDULE_ON_TIME || selectedItem == GROWBOX_CONTROL_SCHEDULE_OFF_TIME) {
        tft->drawString("Rotate: Adjust | Click: H/M", SCREEN_WIDTH / 2, y + STATUS_BAR_HEIGHT / 2);
      } else {
        tft->drawString("Rotate: Adjust | Click: Save", SCREEN_WIDTH / 2, y + STATUS_BAR_HEIGHT / 2);
      }
    } else {
      tft->drawString("Rotate: Select | Click: Action", SCREEN_WIDTH / 2, y + STATUS_BAR_HEIGHT / 2);
    }
  }

  void updateSensorValues() { drawSensorValues(); }
  void updateControls() { drawControls(); drawFooter(); }
};

#endif // GROWBOX_SCREEN_H