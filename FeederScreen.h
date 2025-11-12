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
  bool enabled;  // 이 시간대 활성화 여부
};

class FeederScreen {
private:
  TFT_eSPI* tft;
  FeederData* feederData;
  unsigned long lastUpdate;
  bool needsFullRedraw;

  // 제어 상태
  FeederControlItem selectedItem;
  bool editMode;
  bool editingHour;

  // 스케줄 시간 (3개 시간대)
  FeedScheduleTime scheduleTime1;
  FeedScheduleTime scheduleTime2;
  FeedScheduleTime scheduleTime3;
  bool scheduleActive;

  // 급여 상태
  bool feedingNow;

public:
  FeederScreen(TFT_eSPI* display) :
    tft(display),
    feederData(nullptr),
    lastUpdate(0),
    needsFullRedraw(true),
    selectedItem(FEEDER_CONTROL_NONE),
    editMode(false),
    editingHour(true),
    scheduleActive(false),
    feedingNow(false) {
    // 기본 스케줄: 08:00, 12:00, 18:00
    scheduleTime1.hour = 8;
    scheduleTime1.minute = 0;
    scheduleTime1.enabled = true;

    scheduleTime2.hour = 12;
    scheduleTime2.minute = 0;
    scheduleTime2.enabled = true;

    scheduleTime3.hour = 18;
    scheduleTime3.minute = 0;
    scheduleTime3.enabled = true;
  }

  // 데이터 참조 설정
  void setDataReference(FeederData* feeder) {
    feederData = feeder;
  }

  // 초기화
  void begin() {
    selectedItem = FEEDER_CONTROL_FEED_NOW;
    editMode = false;
    editingHour = true;
    feedingNow = false;
    needsFullRedraw = true;
    draw();
  }

  // 전체 화면 그리기
  void draw() {
    tft->fillScreen(COLOR_BACKGROUND);
    drawHeader();
    drawStatusInfo();
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
        updateStatusInfo();
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
      // 편집 모드: 시간 조절
      if (selectedItem == FEEDER_CONTROL_SCHEDULE_TIME1) {
        adjustScheduleTime(scheduleTime1, direction);
      } else if (selectedItem == FEEDER_CONTROL_SCHEDULE_TIME2) {
        adjustScheduleTime(scheduleTime2, direction);
      } else if (selectedItem == FEEDER_CONTROL_SCHEDULE_TIME3) {
        adjustScheduleTime(scheduleTime3, direction);
      }
      updateControls();
    } else {
      // 선택 모드: 항목 이동
      int newItem = (int)selectedItem + direction;
      if (newItem < 0) newItem = FEEDER_CONTROL_COUNT - 1;
      if (newItem >= FEEDER_CONTROL_COUNT) newItem = 0;
      selectedItem = (FeederControlItem)newItem;
      updateControls();
    }
  }

  // 버튼 클릭 처리
  void onButtonClick() {
    if (selectedItem == FEEDER_CONTROL_BACK) {
      // 뒤로 가기
      if (editMode) {
        editMode = false;
        updateControls();
      }
      return;
    }

    if (selectedItem == FEEDER_CONTROL_FEED_NOW) {
      // 즉시 급여
      feedingNow = true;
      Serial.println("Feeding now!");
      updateControls();
      // 1초 후 자동으로 OFF
      // (실제로는 CAN 응답으로 처리해야 함)
    }

    if (selectedItem == FEEDER_CONTROL_SCHEDULE_ENABLE) {
      // 스케줄 활성화 토글
      scheduleActive = !scheduleActive;
      Serial.printf("Schedule toggled: %s\n", scheduleActive ? "ON" : "OFF");
      updateControls();
    }

    if (selectedItem == FEEDER_CONTROL_SCHEDULE_TIME1 ||
        selectedItem == FEEDER_CONTROL_SCHEDULE_TIME2 ||
        selectedItem == FEEDER_CONTROL_SCHEDULE_TIME3) {
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

  // 선택된 항목
  FeederControlItem getSelectedItem() const {
    return selectedItem;
  }

  // 스케줄 정보 가져오기 (CAN 전송용)
  void getScheduleInfo(uint8_t& active, uint8_t times[3][2]) {
    active = scheduleActive ? 1 : 0;
    times[0][0] = scheduleTime1.hour;
    times[0][1] = scheduleTime1.minute;
    times[1][0] = scheduleTime2.hour;
    times[1][1] = scheduleTime2.minute;
    times[2][0] = scheduleTime3.hour;
    times[2][1] = scheduleTime3.minute;
  }

  bool isFeedingNow() const {
    return feedingNow;
  }

  void resetFeedingNow() {
    feedingNow = false;
  }

private:
  // 스케줄 시간 조절
  void adjustScheduleTime(FeedScheduleTime& time, int direction) {
    if (editingHour) {
      time.hour += direction;
      if (time.hour > 23) time.hour = 0;
      if (time.hour < 0) time.hour = 23;
    } else {
      time.minute += direction * 5;
      if (time.minute > 59) time.minute = 0;
      if (time.minute < 0) time.minute = 55;
    }
  }

  // 헤더
  void drawHeader() {
    tft->fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_HEADER_BG);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextSize(2);
    tft->setCursor(10, 8);
    tft->print("FEEDER DETAIL");
  }

  // 상태 정보 (잔량, 급여 이력)
  void drawStatusInfo() {
    int x = 10;
    int y = HEADER_HEIGHT + 10;
    int lineHeight = 18;

    tft->setTextSize(1);

    // 타이틀
    tft->setTextColor(0x07FF);
    tft->setCursor(x, y);
    tft->print("=== STATUS INFO ===");
    y += lineHeight + 5;

    if (feederData && millis() - feederData->lastUpdate < 5000) {
      // 먹이 잔량
      tft->setTextColor(COLOR_TEXT);
      tft->setCursor(x, y);
      tft->print("Food Level:");
      tft->setCursor(x + 100, y);
      tft->setTextColor(feederData->foodRemaining > 20 ? COLOR_OK : COLOR_WARNING);
      tft->printf("%.0f%%", feederData->foodRemaining);
      y += lineHeight;

      // 프로그레스 바
      int barX = x;
      int barY = y;
      int barW = 150;
      int barH = 10;
      tft->drawRect(barX, barY, barW, barH, COLOR_TEXT);
      int fillW = (barW - 2) * feederData->foodRemaining / 100;
      uint16_t barColor = feederData->foodRemaining > 20 ? COLOR_OK : COLOR_WARNING;
      tft->fillRect(barX + 1, barY + 1, fillW, barH - 2, barColor);
      y += lineHeight + 5;

      // 잔량 경고
      if (feederData->foodRemaining < 20) {
        tft->setTextColor(COLOR_WARNING);
        tft->setCursor(x, y);
        tft->print("! LOW FOOD !");
        y += lineHeight;
      }

      y += 5;

      // 급여 이력
      tft->setTextColor(0x07FF);
      tft->setCursor(x, y);
      tft->print("=== FEED HISTORY ===");
      y += lineHeight + 5;

      // 오늘 급여 횟수
      tft->setTextColor(COLOR_TEXT);
      tft->setCursor(x, y);
      tft->print("Today:");
      tft->setCursor(x + 100, y);
      tft->setTextColor(COLOR_OK);
      tft->printf("%d times", feederData->feedCount);
      y += lineHeight;

      // 마지막 급여 시간
      tft->setTextColor(COLOR_TEXT);
      tft->setCursor(x, y);
      tft->print("Last Feed:");
      tft->setCursor(x + 100, y);
      tft->setTextColor(COLOR_OK);

      // 시간 계산 (간단히 분 단위로)
      unsigned long timeSince = (millis() - feederData->lastFeedTime) / 1000 / 60;
      if (timeSince < 60) {
        tft->printf("%lu min ago", timeSince);
      } else {
        tft->printf("%lu hr ago", timeSince / 60);
      }

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

    // 즉시 급여 버튼
    drawButton(x, y, 140, buttonHeight + 10, "FEED NOW",
               feedingNow ? "FEEDING..." : "",
               selectedItem == FEEDER_CONTROL_FEED_NOW,
               feedingNow ? COLOR_OK : COLOR_TEXT);
    y += buttonHeight + 15;

    // 스케줄 활성화
    drawButton(x, y, 140, buttonHeight, "SCHEDULE",
               scheduleActive ? "ON" : "OFF",
               selectedItem == FEEDER_CONTROL_SCHEDULE_ENABLE,
               scheduleActive ? COLOR_OK : COLOR_INACTIVE);
    y += buttonHeight + 5;

    // 스케줄 시간 1
    drawScheduleTime(x, y, 140, buttonHeight, "Time 1",
                     scheduleTime1,
                     selectedItem == FEEDER_CONTROL_SCHEDULE_TIME1);
    y += buttonHeight + 5;

    // 스케줄 시간 2
    drawScheduleTime(x, y, 140, buttonHeight, "Time 2",
                     scheduleTime2,
                     selectedItem == FEEDER_CONTROL_SCHEDULE_TIME2);
    y += buttonHeight + 5;

    // 스케줄 시간 3
    drawScheduleTime(x, y, 140, buttonHeight, "Time 3",
                     scheduleTime3,
                     selectedItem == FEEDER_CONTROL_SCHEDULE_TIME3);
    y += buttonHeight + 5;

    // 뒤로 가기
    drawButton(x, y, 140, buttonHeight, "BACK",
               "",
               selectedItem == FEEDER_CONTROL_BACK,
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
      tft->setTextSize(1);
      tft->setCursor(x + 5, y + 18);
      tft->print(value);
    }
  }

  // 스케줄 시간 UI
  void drawScheduleTime(int x, int y, int w, int h, const char* label,
                        FeedScheduleTime& time, bool selected) {
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
    tft->setCursor(x + 5, y + 15);

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
      tft->print("Rotate:Adjust | Click:H/M");
    } else {
      tft->print("Rotate:Select | Click:Action");
    }
  }

  // 상태 정보만 업데이트
  void updateStatusInfo() {
    drawStatusInfo();
  }

  // 제어 패널만 업데이트
  void updateControls() {
    drawControls();
    drawFooter();
  }
};

#endif // FEEDER_SCREEN_H
