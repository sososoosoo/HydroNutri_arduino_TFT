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
  NutrientData* nutrientData;
  unsigned long lastUpdate;
  bool needsFullRedraw;

  // 제어 상태
  NutrientControlItem selectedItem;
  NutrientStatus operationStatus;

public:
  NutrientScreen(TFT_eSPI* display) :
    tft(display),
    nutrientData(nullptr),
    lastUpdate(0),
    needsFullRedraw(true),
    selectedItem(NUTRIENT_CONTROL_NONE),
    operationStatus(NUTRIENT_IDLE) {}

  // 데이터 참조 설정
  void setDataReference(NutrientData* nutrient) {
    nutrientData = nutrient;
  }

  // 초기화
  void begin() {
    selectedItem = NUTRIENT_CONTROL_SUPPLY;
    operationStatus = NUTRIENT_IDLE;
    needsFullRedraw = true;
    draw();
  }

  // 전체 화면 그리기
  void draw() {
    tft->fillScreen(COLOR_BACKGROUND);
    drawHeader();
    drawSystemInfo();
    drawStepperStatus();
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
        updateSystemInfo();
        updateStepperStatus();
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
    // 선택 모드: 항목 이동
    int newItem = (int)selectedItem + direction;
    if (newItem < 0) newItem = NUTRIENT_CONTROL_COUNT - 1;
    if (newItem >= NUTRIENT_CONTROL_COUNT) newItem = 0;
    selectedItem = (NutrientControlItem)newItem;
    updateControls();
  }

  // 버튼 클릭 처리
  void onButtonClick() {
    if (selectedItem == NUTRIENT_CONTROL_BACK) {
      // 뒤로 가기
      Serial.println("Back to Dashboard");
      return;
    }

    if (selectedItem == NUTRIENT_CONTROL_SUPPLY) {
      // 공급 시작
      operationStatus = NUTRIENT_SUPPLYING;
      Serial.println("Nutrient supply started");
      updateControls();
    }

    if (selectedItem == NUTRIENT_CONTROL_STOP) {
      // 공급 정지
      operationStatus = NUTRIENT_STOPPED;
      Serial.println("Nutrient supply stopped");
      updateControls();
    }
  }

  // 선택된 항목
  NutrientControlItem getSelectedItem() const {
    return selectedItem;
  }

  // 공급 상태
  NutrientStatus getOperationStatus() const {
    return operationStatus;
  }

private:
  // 헤더
  void drawHeader() {
    tft->fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_HEADER_BG);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextSize(2);
    tft->setCursor(10, 8);
    tft->print("NUTRIENT DETAIL");
  }

  // 시스템 정보 (배합, 공급량, 잔량)
  void drawSystemInfo() {
    int x = 10;
    int y = HEADER_HEIGHT + 10;
    int lineHeight = 18;

    tft->setTextSize(1);

    // 타이틀
    tft->setTextColor(0x07FF);
    tft->setCursor(x, y);
    tft->print("=== SYSTEM INFO ===");
    y += lineHeight + 5;

    if (nutrientData && millis() - nutrientData->lastUpdate < 5000) {
      // 배합 비율
      tft->setTextColor(COLOR_TEXT);
      tft->setCursor(x, y);
      tft->print("Mix Ratio:");
      tft->setCursor(x + 100, y);
      tft->setTextColor(COLOR_OK);
      tft->printf("%.1f", nutrientData->mixRatio);
      y += lineHeight;

      // 공급량
      tft->setTextColor(COLOR_TEXT);
      tft->setCursor(x, y);
      tft->print("Supply:");
      tft->setCursor(x + 100, y);
      tft->setTextColor(COLOR_OK);
      tft->printf("%.1f L", nutrientData->supplyAmount);
      y += lineHeight;

      // 잔량
      tft->setTextColor(COLOR_TEXT);
      tft->setCursor(x, y);
      tft->print("Remaining:");
      tft->setCursor(x + 100, y);
      tft->setTextColor(nutrientData->remainingAmount > 20 ? COLOR_OK : COLOR_WARNING);
      tft->printf("%.1f L", nutrientData->remainingAmount);
      y += lineHeight;

      // 잔량 경고
      if (nutrientData->remainingAmount < 20) {
        y += 5;
        tft->setTextColor(COLOR_WARNING);
        tft->setCursor(x, y);
        tft->print("! LOW NUTRIENT !");
      }

    } else {
      tft->setTextColor(COLOR_ERROR);
      tft->setCursor(x, y + 20);
      tft->print("No Data Available");
    }
  }

  // 스텝모터 상태 (3개 채널)
  void drawStepperStatus() {
    int x = 10;
    int y = HEADER_HEIGHT + 100;
    int lineHeight = 18;

    tft->setTextSize(1);

    // 타이틀
    tft->setTextColor(0x07FF);
    tft->setCursor(x, y);
    tft->print("=== STEPPER MOTORS ===");
    y += lineHeight + 5;

    if (nutrientData && millis() - nutrientData->lastUpdate < 5000) {
      // 채널 1
      drawStepperChannel(x, y, "CH1", nutrientData->channel1Steps);
      y += lineHeight + 8;

      // 채널 2
      drawStepperChannel(x, y, "CH2", nutrientData->channel2Steps);
      y += lineHeight + 8;

      // 채널 3
      drawStepperChannel(x, y, "CH3", nutrientData->channel3Steps);

    } else {
      tft->setTextColor(COLOR_ERROR);
      tft->setCursor(x, y);
      tft->print("No Data");
    }
  }

  // 스텝모터 채널 표시
  void drawStepperChannel(int x, int y, const char* label, uint8_t steps) {
    tft->setTextSize(1);
    tft->setTextColor(COLOR_TEXT);
    tft->setCursor(x, y);
    tft->printf("%s:", label);

    tft->setCursor(x + 40, y);
    tft->setTextColor(COLOR_OK);
    tft->printf("%3d steps", steps);

    // 프로그레스 바 (0-255 범위를 0-100%로 표시)
    int barX = x + 120;
    int barY = y;
    int barW = 60;
    int barH = 8;

    tft->drawRect(barX, barY, barW, barH, COLOR_TEXT);
    int fillW = (barW - 2) * steps / 255;
    tft->fillRect(barX + 1, barY + 1, fillW, barH - 2, COLOR_OK);
  }

  // 제어 패널
  void drawControls() {
    int x = 180;
    int y = HEADER_HEIGHT + 10;
    int lineHeight = 18;
    int buttonHeight = 35;

    tft->setTextSize(1);

    // 타이틀
    tft->setTextColor(0x07FF);
    tft->setCursor(x, y);
    tft->print("=== CONTROLS ===");
    y += lineHeight + 5;

    // 현재 상태 표시
    y += 5;
    tft->setTextColor(COLOR_TEXT);
    tft->setCursor(x, y);
    tft->print("Status:");
    y += 12;
    tft->setCursor(x, y);

    switch (operationStatus) {
      case NUTRIENT_IDLE:
        tft->setTextColor(COLOR_INACTIVE);
        tft->print("IDLE");
        break;
      case NUTRIENT_SUPPLYING:
        tft->setTextColor(COLOR_OK);
        tft->print("SUPPLYING");
        break;
      case NUTRIENT_STOPPED:
        tft->setTextColor(COLOR_WARNING);
        tft->print("STOPPED");
        break;
    }
    y += lineHeight + 10;

    // 공급 시작 버튼
    drawButton(x, y, 130, buttonHeight, "SUPPLY",
               operationStatus == NUTRIENT_SUPPLYING ? "RUNNING" : "START",
               selectedItem == NUTRIENT_CONTROL_SUPPLY,
               operationStatus == NUTRIENT_SUPPLYING ? COLOR_OK : COLOR_TEXT);
    y += buttonHeight + 10;

    // 정지 버튼
    drawButton(x, y, 130, buttonHeight, "STOP",
               "",
               selectedItem == NUTRIENT_CONTROL_STOP,
               COLOR_ERROR);
    y += buttonHeight + 10;

    // 뒤로 가기
    drawButton(x, y, 130, buttonHeight, "BACK",
               "",
               selectedItem == NUTRIENT_CONTROL_BACK,
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
      tft->setCursor(x + 5, y + 20);
      tft->print(value);
    }
  }

  // 푸터
  void drawFooter() {
    int y = SCREEN_HEIGHT - STATUS_BAR_HEIGHT;
    tft->fillRect(0, y, SCREEN_WIDTH, STATUS_BAR_HEIGHT, COLOR_PANEL_BG);

    tft->setTextSize(1);
    tft->setTextColor(COLOR_TEXT);
    tft->setCursor(5, y + 5);
    tft->print("Rotate:Select | Click:Action");
  }

  // 시스템 정보만 업데이트
  void updateSystemInfo() {
    drawSystemInfo();
  }

  // 스텝모터 상태만 업데이트
  void updateStepperStatus() {
    drawStepperStatus();
  }

  // 제어 패널만 업데이트
  void updateControls() {
    drawControls();
    drawFooter();
  }
};

#endif // NUTRIENT_SCREEN_H
