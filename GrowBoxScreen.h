#ifndef GROWBOX_SCREEN_H
#define GROWBOX_SCREEN_H

#include <TFT_eSPI.h>
#include "Config.h"

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
  GrowBoxData* growBoxData;
  unsigned long lastUpdate;
  bool needsFullRedraw;

  // 제어 상태
  GrowBoxControlItem selectedItem;
  bool editMode;  // 편집 모드
  bool editingHour;  // true: 시간 편집, false: 분 편집

  // 스케줄 시간
  ScheduleTime scheduleOnTime;
  ScheduleTime scheduleOffTime;

public:
  GrowBoxScreen(TFT_eSPI* display) :
    tft(display),
    growBoxData(nullptr),
    lastUpdate(0),
    needsFullRedraw(true),
    selectedItem(GROWBOX_CONTROL_NONE),
    editMode(false),
    editingHour(true) {
    // 기본 스케줄: 08:00 ON, 20:00 OFF
    scheduleOnTime.hour = 8;
    scheduleOnTime.minute = 0;
    scheduleOffTime.hour = 20;
    scheduleOffTime.minute = 0;
  }

  // 데이터 참조 설정
  void setDataReference(GrowBoxData* growBox) {
    growBoxData = growBox;
  }

  // 초기화
  void begin() {
    selectedItem = GROWBOX_CONTROL_LED_BRIGHTNESS;
    editMode = false;
    editingHour = true;
    needsFullRedraw = true;
    draw();
  }

  // 전체 화면 그리기
  void draw() {
    tft->fillScreen(COLOR_BACKGROUND);
    drawHeader();
    drawSensorValues();
    drawControls();
    drawFooter();
    needsFullRedraw = false;
    lastUpdate = millis();
  }

  // 업데이트
  void update() {
    if (millis() - lastUpdate >= UPDATE_INTERVAL) {
      if (needsFullRedraw) {
        draw();
      } else {
        updateSensorValues();
        updateControls();
      }
      lastUpdate = millis();
    }
  }

  // 강제 재그리기
  void forceRedraw() {
    needsFullRedraw = true;
  }

  // 로터리 엔코더 회전 처리
  void onEncoderRotate(int direction) {
    if (editMode) {
      // 편집 모드
      if (selectedItem == GROWBOX_CONTROL_LED_BRIGHTNESS && growBoxData) {
        // LED 밝기 조절
        int newBrightness = growBoxData->ledBrightness + (direction * 5);
        if (newBrightness < 0) newBrightness = 0;
        if (newBrightness > 100) newBrightness = 100;
        growBoxData->ledBrightness = newBrightness;
        Serial.printf("LED brightness changed: %d%%\n", newBrightness);
        updateControls();

      } else if (selectedItem == GROWBOX_CONTROL_SCHEDULE_ON_TIME) {
        // ON 시간 편집
        adjustScheduleTime(scheduleOnTime, direction);
        updateControls();

      } else if (selectedItem == GROWBOX_CONTROL_SCHEDULE_OFF_TIME) {
        // OFF 시간 편집
        adjustScheduleTime(scheduleOffTime, direction);
        updateControls();
      }
    } else {
      // 선택 모드: 항목 이동
      int newItem = (int)selectedItem + direction;
      if (newItem < 0) newItem = GROWBOX_CONTROL_COUNT - 1;
      if (newItem >= GROWBOX_CONTROL_COUNT) newItem = 0;
      selectedItem = (GrowBoxControlItem)newItem;
      updateControls();
    }
  }

  // 버튼 클릭 처리
  void onButtonClick() {
    if (selectedItem == GROWBOX_CONTROL_BACK) {
      // 뒤로 가기
      if (editMode) {
        editMode = false;
        updateControls();
      }
      return;
    }

    if (selectedItem == GROWBOX_CONTROL_LED_BRIGHTNESS) {
      // LED 밝기 편집 모드 토글
      editMode = !editMode;
      Serial.printf("LED brightness edit mode: %s\n", editMode ? "ON" : "OFF");
      updateControls();
    }

    if (selectedItem == GROWBOX_CONTROL_SCHEDULE_ENABLE && growBoxData) {
      // 스케줄 활성화 토글
      growBoxData->ledScheduleActive = !growBoxData->ledScheduleActive;
      Serial.printf("Schedule toggled: %s\n", growBoxData->ledScheduleActive ? "ON" : "OFF");
      updateControls();
    }

    if (selectedItem == GROWBOX_CONTROL_SCHEDULE_ON_TIME ||
        selectedItem == GROWBOX_CONTROL_SCHEDULE_OFF_TIME) {
      if (editMode) {
        // 시간/분 편집 전환
        editingHour = !editingHour;
        Serial.printf("Editing: %s\n", editingHour ? "HOUR" : "MINUTE");
      } else {
        // 편집 모드 진입
        editMode = true;
        editingHour = true;
        Serial.println("Schedule time edit mode ON");
      }
      updateControls();
    }
  }

  // 편집 모드 여부
  bool isEditMode() const {
    return editMode;
  }

  // 선택된 항목
  GrowBoxControlItem getSelectedItem() const {
    return selectedItem;
  }

private:
  // 스케줄 시간 조절
  void adjustScheduleTime(ScheduleTime& time, int direction) {
    if (editingHour) {
      // 시간 조절
      time.hour += direction;
      if (time.hour > 23) time.hour = 0;
      if (time.hour < 0) time.hour = 23;
      Serial.printf("Hour adjusted: %02d\n", time.hour);
    } else {
      // 분 조절 (5분 단위)
      time.minute += direction * 5;
      if (time.minute > 59) time.minute = 0;
      if (time.minute < 0) time.minute = 55;
      Serial.printf("Minute adjusted: %02d\n", time.minute);
    }
  }

  // 헤더
  void drawHeader() {
    tft->fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_HEADER_BG);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextSize(2);
    tft->setCursor(10, 8);
    tft->print("GROWBOX DETAIL");
  }

  // 센서값 표시
  void drawSensorValues() {
    int x = 10;
    int y = HEADER_HEIGHT + 10;
    int lineHeight = 18;

    tft->setTextSize(1);

    // 타이틀
    tft->setTextColor(0x07FF);
    tft->setCursor(x, y);
    tft->print("=== SENSOR DATA ===");
    y += lineHeight + 5;

    if (growBoxData && millis() - growBoxData->lastUpdate < 5000) {
      // 온도
      tft->setTextColor(COLOR_TEXT);
      tft->setCursor(x, y);
      tft->print("Temperature:");
      tft->setCursor(x + 100, y);
      tft->setTextColor((growBoxData->temperature >= 18 && growBoxData->temperature <= 28) ?
                       COLOR_OK : COLOR_WARNING);
      tft->printf("%.1f C", growBoxData->temperature);
      y += lineHeight;

      // 습도
      tft->setTextColor(COLOR_TEXT);
      tft->setCursor(x, y);
      tft->print("Humidity:");
      tft->setCursor(x + 100, y);
      tft->setTextColor((growBoxData->humidity >= 40 && growBoxData->humidity <= 70) ?
                       COLOR_OK : COLOR_WARNING);
      tft->printf("%.0f %%", growBoxData->humidity);
      y += lineHeight;

      // 누수 감지
      tft->setTextColor(COLOR_TEXT);
      tft->setCursor(x, y);
      tft->print("Leak Status:");
      tft->setCursor(x + 100, y);
      if (growBoxData->leakDetected) {
        tft->setTextColor(COLOR_ERROR);
        tft->print("DETECTED!");
      } else {
        tft->setTextColor(COLOR_OK);
        tft->print("OK");
      }
      y += lineHeight;

      // LED 현재 상태
      tft->setTextColor(COLOR_TEXT);
      tft->setCursor(x, y);
      tft->print("LED Status:");
      tft->setCursor(x + 100, y);
      tft->setTextColor(COLOR_OK);
      tft->printf("%d%%", growBoxData->ledBrightness);

    } else {
      tft->setTextColor(COLOR_ERROR);
      tft->setCursor(x, y + 30);
      tft->print("No Data Available");
    }
  }

  // 제어 패널
  void drawControls() {
    int x = 170;
    int y = HEADER_HEIGHT + 10;
    int lineHeight = 18;
    int buttonHeight = 28;

    tft->setTextSize(1);

    // 타이틀
    tft->setTextColor(0x07FF);
    tft->setCursor(x, y);
    tft->print("=== CONTROLS ===");
    y += lineHeight + 5;

    if (!growBoxData) return;

    // LED 밝기 제어
    drawBrightnessControl(x, y, 140, buttonHeight + 20,
                          selectedItem == GROWBOX_CONTROL_LED_BRIGHTNESS);
    y += buttonHeight + 25;

    // 스케줄 활성화
    drawButton(x, y, 140, buttonHeight, "SCHEDULE",
               growBoxData->ledScheduleActive ? "ON" : "OFF",
               selectedItem == GROWBOX_CONTROL_SCHEDULE_ENABLE,
               growBoxData->ledScheduleActive ? COLOR_OK : COLOR_INACTIVE);
    y += buttonHeight + 5;

    // 스케줄 ON 시간
    drawScheduleTime(x, y, 140, buttonHeight, "ON TIME",
                     scheduleOnTime,
                     selectedItem == GROWBOX_CONTROL_SCHEDULE_ON_TIME);
    y += buttonHeight + 5;

    // 스케줄 OFF 시간
    drawScheduleTime(x, y, 140, buttonHeight, "OFF TIME",
                     scheduleOffTime,
                     selectedItem == GROWBOX_CONTROL_SCHEDULE_OFF_TIME);
    y += buttonHeight + 5;

    // 뒤로 가기
    drawButton(x, y, 140, buttonHeight, "BACK",
               "",
               selectedItem == GROWBOX_CONTROL_BACK,
               COLOR_INACTIVE);
  }

  // 버튼 그리기
  void drawButton(int x, int y, int w, int h, const char* label,
                  const char* value, bool selected, uint16_t valueColor) {
    uint16_t borderColor = selected ? COLOR_WARNING : COLOR_TEXT;
    tft->drawRect(x, y, w, h, borderColor);
    if (selected) {
      tft->drawRect(x + 1, y + 1, w - 2, h - 2, borderColor);
    }

    tft->setTextColor(COLOR_TEXT);
    tft->setTextSize(1);
    tft->setCursor(x + 5, y + 5);
    tft->print(label);

    if (strlen(value) > 0) {
      tft->setTextColor(valueColor);
      tft->setTextSize(2);
      tft->setCursor(x + 5, y + 15);
      tft->print(value);
    }
  }

  // 밝기 제어 UI
  void drawBrightnessControl(int x, int y, int w, int h, bool selected) {
    uint16_t borderColor = selected ? COLOR_WARNING : COLOR_TEXT;
    tft->drawRect(x, y, w, h, borderColor);
    if (selected) {
      tft->drawRect(x + 1, y + 1, w - 2, h - 2, borderColor);
    }

    tft->setTextColor(COLOR_TEXT);
    tft->setTextSize(1);
    tft->setCursor(x + 5, y + 5);
    tft->print("LED BRIGHTNESS");

    tft->setTextColor(COLOR_OK);
    tft->setTextSize(2);
    tft->setCursor(x + 5, y + 18);
    tft->printf("%d%%", growBoxData->ledBrightness);

    if (editMode && selected) {
      tft->setTextColor(COLOR_WARNING);
      tft->setTextSize(1);
      tft->setCursor(x + 70, y + 20);
      tft->print("EDIT");
    }

    // 프로그레스 바
    int barX = x + 5;
    int barY = y + h - 8;
    int barW = w - 10;
    int barH = 5;

    tft->drawRect(barX, barY, barW, barH, COLOR_TEXT);
    int fillW = (barW - 2) * growBoxData->ledBrightness / 100;
    tft->fillRect(barX + 1, barY + 1, fillW, barH - 2, COLOR_OK);
  }

  // 스케줄 시간 UI
  void drawScheduleTime(int x, int y, int w, int h, const char* label,
                        ScheduleTime& time, bool selected) {
    uint16_t borderColor = selected ? COLOR_WARNING : COLOR_TEXT;
    tft->drawRect(x, y, w, h, borderColor);
    if (selected) {
      tft->drawRect(x + 1, y + 1, w - 2, h - 2, borderColor);
    }

    tft->setTextColor(COLOR_TEXT);
    tft->setTextSize(1);
    tft->setCursor(x + 5, y + 5);
    tft->print(label);

    // 시간 표시
    tft->setTextSize(2);
    tft->setCursor(x + 5, y + 15);

    // 편집 중인 부분 강조
    if (editMode && selected) {
      if (editingHour) {
        tft->setTextColor(COLOR_WARNING);
        tft->printf("%02d", time.hour);
        tft->setTextColor(COLOR_TEXT);
        tft->printf(":%02d", time.minute);
      } else {
        tft->setTextColor(COLOR_TEXT);
        tft->printf("%02d:", time.hour);
        tft->setTextColor(COLOR_WARNING);
        tft->printf("%02d", time.minute);
      }
    } else {
      tft->setTextColor(COLOR_OK);
      tft->printf("%02d:%02d", time.hour, time.minute);
    }
  }

  // 푸터
  void drawFooter() {
    int y = SCREEN_HEIGHT - STATUS_BAR_HEIGHT;
    tft->fillRect(0, y, SCREEN_WIDTH, STATUS_BAR_HEIGHT, COLOR_PANEL_BG);

    tft->setTextSize(1);
    tft->setTextColor(COLOR_TEXT);
    tft->setCursor(5, y + 5);

    if (editMode) {
      if (selectedItem == GROWBOX_CONTROL_SCHEDULE_ON_TIME ||
          selectedItem == GROWBOX_CONTROL_SCHEDULE_OFF_TIME) {
        tft->print("Rotate:Adjust | Click:H/M");
      } else {
        tft->print("Rotate:Adjust | Click:Save");
      }
    } else {
      tft->print("Rotate:Select | Click:Action");
    }
  }

  // 센서값만 업데이트
  void updateSensorValues() {
    drawSensorValues();
  }

  // 제어 패널만 업데이트
  void updateControls() {
    drawControls();
    drawFooter();
  }
};

#endif // GROWBOX_SCREEN_H
