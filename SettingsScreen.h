#ifndef SETTINGS_SCREEN_H
#define SETTINGS_SCREEN_H

#include <TFT_eSPI.h>
#include "Config.h"

// 설정 항목
enum SettingsItem {
  SETTINGS_SCREEN_OFF = 0,
  SETTINGS_MODULE_TANK,
  SETTINGS_MODULE_GROWBOX,
  SETTINGS_MODULE_NUTRIENT,
  SETTINGS_MODULE_FEEDER,
  SETTINGS_ROUTINE_ENABLE,
  SETTINGS_ROUTINE_TIME,
  SETTINGS_TIME_SYNC,
  SETTINGS_FW_VERSION,
  SETTINGS_FACTORY_RESET,
  SETTINGS_BACK,
  SETTINGS_COUNT
};

class SettingsScreen {
private:
  TFT_eSPI* tft;
  unsigned long lastUpdate;
  bool needsFullRedraw;

  // 설정 데이터
  SystemSettings* settings;

  // UI 상태
  SettingsItem selectedItem;
  int scrollPosition;
  bool editMode;
  bool editingHour;  // 루틴 시간 편집 시 사용

  const int ITEMS_PER_PAGE = 7;  // 페이지당 표시할 항목 수

public:
  SettingsScreen(TFT_eSPI* display) :
    tft(display),
    lastUpdate(0),
    needsFullRedraw(true),
    settings(nullptr),
    selectedItem(SETTINGS_SCREEN_OFF),
    scrollPosition(0),
    editMode(false),
    editingHour(true) {}

  // 설정 데이터 참조
  void setSettingsReference(SystemSettings* sys) {
    settings = sys;
  }

  // 초기화
  void begin() {
    selectedItem = SETTINGS_SCREEN_OFF;
    scrollPosition = 0;
    editMode = false;
    needsFullRedraw = true;
    draw();
  }

  // 전체 화면 그리기
  void draw() {
    tft->fillScreen(COLOR_BACKGROUND);
    drawHeader();
    drawSettingsList();
    drawFooter();
    needsFullRedraw = false;
    lastUpdate = millis();
  }

  // 업데이트
  void update() {
    if (millis() - lastUpdate >= UPDATE_INTERVAL) {
      if (needsFullRedraw) {
        draw();
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
      // 편집 모드: 값 조절
      handleValueEdit(direction);
    } else {
      // 선택 모드: 항목 이동
      int newItem = (int)selectedItem + direction;
      if (newItem < 0) newItem = SETTINGS_COUNT - 1;
      if (newItem >= SETTINGS_COUNT) newItem = 0;
      selectedItem = (SettingsItem)newItem;

      // 스크롤 조정
      if (selectedItem < scrollPosition) {
        scrollPosition = selectedItem;
      } else if (selectedItem >= scrollPosition + ITEMS_PER_PAGE) {
        scrollPosition = selectedItem - ITEMS_PER_PAGE + 1;
      }
    }
    needsFullRedraw = true;
  }

  // 버튼 클릭 처리
  void onButtonClick() {
    if (!settings) return;

    switch (selectedItem) {
      case SETTINGS_SCREEN_OFF:
        // 화면 끄기 시간 순환
        settings->screenOffTime = (ScreenOffTime)((settings->screenOffTime + 1) % SCREEN_OFF_COUNT);
        Serial.printf("Screen off time: %s\n", getScreenOffTimeName(settings->screenOffTime));
        break;

      case SETTINGS_MODULE_TANK:
        settings->tankEnabled = !settings->tankEnabled;
        Serial.printf("Tank module: %s\n", settings->tankEnabled ? "ON" : "OFF");
        break;

      case SETTINGS_MODULE_GROWBOX:
        settings->growBoxEnabled = !settings->growBoxEnabled;
        Serial.printf("GrowBox module: %s\n", settings->growBoxEnabled ? "ON" : "OFF");
        break;

      case SETTINGS_MODULE_NUTRIENT:
        settings->nutrientEnabled = !settings->nutrientEnabled;
        Serial.printf("Nutrient module: %s\n", settings->nutrientEnabled ? "ON" : "OFF");
        break;

      case SETTINGS_MODULE_FEEDER:
        settings->feederEnabled = !settings->feederEnabled;
        Serial.printf("Feeder module: %s\n", settings->feederEnabled ? "ON" : "OFF");
        break;

      case SETTINGS_ROUTINE_ENABLE:
        settings->autoRoutineEnabled = !settings->autoRoutineEnabled;
        Serial.printf("Auto routine: %s\n", settings->autoRoutineEnabled ? "ON" : "OFF");
        break;

      case SETTINGS_ROUTINE_TIME:
        if (editMode) {
          // 시간/분 전환
          editingHour = !editingHour;
        } else {
          // 편집 모드 진입
          editMode = true;
          editingHour = true;
        }
        Serial.printf("Routine time edit: %s\n", editMode ? "ON" : "OFF");
        break;

      case SETTINGS_TIME_SYNC:
        // 시간 동기화 실행
        syncTime();
        break;

      case SETTINGS_FW_VERSION:
        // 버전 정보 표시 (아무 동작 없음)
        Serial.printf("FW Version: %s\n", FW_VERSION);
        break;

      case SETTINGS_FACTORY_RESET:
        // 공장 초기화 확인 다이얼로그 필요 (여기서는 즉시 실행)
        factoryReset();
        break;

      case SETTINGS_BACK:
        // 뒤로 가기
        if (editMode) {
          editMode = false;
        }
        return;
    }

    needsFullRedraw = true;
  }

  // 선택된 항목
  SettingsItem getSelectedItem() const {
    return selectedItem;
  }

private:
  // 헤더
  void drawHeader() {
    tft->fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_HEADER_BG);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextSize(2);
    tft->setCursor(10, 8);
    tft->print("SETTINGS");
  }

  // 설정 목록
  void drawSettingsList() {
    int y = HEADER_HEIGHT + 5;
    int lineHeight = 25;
    int displayCount = 0;

    if (!settings) return;

    // 스크롤 범위 체크
    if (scrollPosition >= SETTINGS_COUNT) {
      scrollPosition = SETTINGS_COUNT - 1;
    }

    // 표시할 항목
    int startIdx = scrollPosition;
    int endIdx = min(startIdx + ITEMS_PER_PAGE, (int)SETTINGS_COUNT);

    for (int i = startIdx; i < endIdx && displayCount < ITEMS_PER_PAGE; i++, displayCount++) {
      drawSettingsItem(5, y, (SettingsItem)i, i == (int)selectedItem);
      y += lineHeight;
    }

    // 스크롤 인디케이터
    if (SETTINGS_COUNT > ITEMS_PER_PAGE) {
      drawScrollIndicator();
    }
  }

  // 설정 항목 그리기
  void drawSettingsItem(int x, int y, SettingsItem item, bool selected) {
    // 선택 표시
    if (selected) {
      tft->fillRect(x - 2, y - 2, SCREEN_WIDTH - 10, 23, COLOR_PANEL_BG);
      tft->drawRect(x - 2, y - 2, SCREEN_WIDTH - 10, 23, COLOR_WARNING);
    }

    tft->setTextSize(1);
    tft->setTextColor(COLOR_TEXT);
    tft->setCursor(x, y);

    // 항목별 표시
    switch (item) {
      case SETTINGS_SCREEN_OFF:
        tft->print("Screen Off:");
        tft->setCursor(x + 150, y);
        tft->setTextColor(COLOR_OK);
        tft->print(getScreenOffTimeName(settings->screenOffTime));
        break;

      case SETTINGS_MODULE_TANK:
        tft->print("Tank Module:");
        tft->setCursor(x + 150, y);
        tft->setTextColor(settings->tankEnabled ? COLOR_OK : COLOR_INACTIVE);
        tft->print(settings->tankEnabled ? "ON" : "OFF");
        break;

      case SETTINGS_MODULE_GROWBOX:
        tft->print("GrowBox Module:");
        tft->setCursor(x + 150, y);
        tft->setTextColor(settings->growBoxEnabled ? COLOR_OK : COLOR_INACTIVE);
        tft->print(settings->growBoxEnabled ? "ON" : "OFF");
        break;

      case SETTINGS_MODULE_NUTRIENT:
        tft->print("Nutrient Module:");
        tft->setCursor(x + 150, y);
        tft->setTextColor(settings->nutrientEnabled ? COLOR_OK : COLOR_INACTIVE);
        tft->print(settings->nutrientEnabled ? "ON" : "OFF");
        break;

      case SETTINGS_MODULE_FEEDER:
        tft->print("Feeder Module:");
        tft->setCursor(x + 150, y);
        tft->setTextColor(settings->feederEnabled ? COLOR_OK : COLOR_INACTIVE);
        tft->print(settings->feederEnabled ? "ON" : "OFF");
        break;

      case SETTINGS_ROUTINE_ENABLE:
        tft->print("Auto Routine:");
        tft->setCursor(x + 150, y);
        tft->setTextColor(settings->autoRoutineEnabled ? COLOR_OK : COLOR_INACTIVE);
        tft->print(settings->autoRoutineEnabled ? "ON" : "OFF");
        break;

      case SETTINGS_ROUTINE_TIME:
        tft->print("Routine Time:");
        tft->setCursor(x + 150, y);
        if (editMode && selected) {
          if (editingHour) {
            tft->setTextColor(COLOR_WARNING);
            tft->printf("%02d", settings->routineHour);
            tft->setTextColor(COLOR_OK);
            tft->printf(":%02d", settings->routineMinute);
          } else {
            tft->setTextColor(COLOR_OK);
            tft->printf("%02d:", settings->routineHour);
            tft->setTextColor(COLOR_WARNING);
            tft->printf("%02d", settings->routineMinute);
          }
        } else {
          tft->setTextColor(COLOR_OK);
          tft->printf("%02d:%02d", settings->routineHour, settings->routineMinute);
        }
        break;

      case SETTINGS_TIME_SYNC:
        tft->print("Time Sync:");
        tft->setCursor(x + 150, y);
        tft->setTextColor(0x07FF);
        tft->print("SYNC NOW");
        break;

      case SETTINGS_FW_VERSION:
        tft->print("FW Version:");
        tft->setCursor(x + 150, y);
        tft->setTextColor(COLOR_INACTIVE);
        tft->print(FW_VERSION);
        break;

      case SETTINGS_FACTORY_RESET:
        tft->setTextColor(COLOR_ERROR);
        tft->print("Factory Reset:");
        tft->setCursor(x + 150, y);
        tft->print("RESET");
        break;

      case SETTINGS_BACK:
        tft->setTextColor(COLOR_TEXT);
        tft->print("Back to Dashboard");
        break;

      default:
        break;
    }
  }

  // 스크롤 인디케이터
  void drawScrollIndicator() {
    int barX = SCREEN_WIDTH - 8;
    int barY = HEADER_HEIGHT + 10;
    int barH = SCREEN_HEIGHT - HEADER_HEIGHT - STATUS_BAR_HEIGHT - 20;

    tft->drawRect(barX, barY, 5, barH, COLOR_TEXT);

    int indicatorH = max(10, barH * ITEMS_PER_PAGE / SETTINGS_COUNT);
    int indicatorY = barY + (barH - indicatorH) * scrollPosition / max(1, SETTINGS_COUNT - ITEMS_PER_PAGE);
    tft->fillRect(barX + 1, indicatorY, 3, indicatorH, COLOR_OK);
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
      tft->print("Rotate:Select | Click:Toggle");
    }
  }

  // 값 편집 처리
  void handleValueEdit(int direction) {
    if (selectedItem == SETTINGS_ROUTINE_TIME) {
      if (editingHour) {
        settings->routineHour += direction;
        if (settings->routineHour > 23) settings->routineHour = 0;
        if (settings->routineHour < 0) settings->routineHour = 23;
      } else {
        settings->routineMinute += direction * 5;
        if (settings->routineMinute > 59) settings->routineMinute = 0;
        if (settings->routineMinute < 0) settings->routineMinute = 55;
      }
    }
  }

  // 화면 끄기 시간 이름
  const char* getScreenOffTimeName(ScreenOffTime time) {
    switch (time) {
      case SCREEN_OFF_NEVER: return "Never";
      case SCREEN_OFF_1MIN: return "1 min";
      case SCREEN_OFF_5MIN: return "5 min";
      case SCREEN_OFF_10MIN: return "10 min";
      case SCREEN_OFF_30MIN: return "30 min";
      default: return "Unknown";
    }
  }

  // 시간 동기화
  void syncTime() {
    Serial.println("Time sync requested");
    // TODO: RTC 또는 서버와 시간 동기화
    // 여기서는 시뮬레이션
    tft->fillRect(60, 100, 200, 40, COLOR_PANEL_BG);
    tft->drawRect(60, 100, 200, 40, COLOR_OK);
    tft->setTextSize(1);
    tft->setTextColor(COLOR_OK);
    tft->setCursor(100, 115);
    tft->print("Time Synced!");
    delay(1500);
    needsFullRedraw = true;
  }

  // 공장 초기화
  void factoryReset() {
    Serial.println("Factory reset requested");

    // 확인 메시지
    tft->fillRect(40, 80, 240, 80, COLOR_ERROR);
    tft->drawRect(40, 80, 240, 80, COLOR_TEXT);
    tft->setTextSize(1);
    tft->setTextColor(COLOR_TEXT);
    tft->setCursor(60, 95);
    tft->print("Factory Reset!");
    tft->setCursor(60, 110);
    tft->print("All settings cleared");
    tft->setCursor(60, 125);
    tft->print("System will restart...");

    delay(2000);

    // 설정 초기화
    settings->screenOffTime = SCREEN_OFF_NEVER;
    settings->tankEnabled = true;
    settings->growBoxEnabled = true;
    settings->nutrientEnabled = true;
    settings->feederEnabled = true;
    settings->autoRoutineEnabled = false;
    settings->routineHour = 8;
    settings->routineMinute = 0;

    Serial.println("Settings reset to default");

    needsFullRedraw = true;
  }
};

#endif // SETTINGS_SCREEN_H
