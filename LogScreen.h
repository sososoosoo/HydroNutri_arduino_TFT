#ifndef LOG_SCREEN_H
#define LOG_SCREEN_H

#include <TFT_eSPI.h>
#include "Config.h"

// 필터 타입
enum LogFilter {
  FILTER_ALL = 0,
  FILTER_INFO,
  FILTER_WARNING,
  FILTER_ERROR,
  FILTER_EVENT,
  FILTER_COUNT
};

// 정렬 타입
enum LogSort {
  SORT_NEWEST = 0,
  SORT_OLDEST
};

class LogScreen {
private:
  TFT_eSPI* tft;
  unsigned long lastUpdate;
  bool needsFullRedraw;

  // 로그 저장소
  LogEntry logs[MAX_LOG_ENTRIES];
  int logCount;

  // UI 상태
  int scrollPosition;       // 스크롤 위치
  int selectedIndex;        // 선택된 항목 인덱스
  LogFilter currentFilter;  // 현재 필터
  LogSort currentSort;      // 현재 정렬
  bool showingMenu;         // 메뉴 표시 여부

  // 메뉴 항목
  enum MenuOption {
    MENU_FILTER = 0,
    MENU_SORT,
    MENU_CLEAR_ALL,
    MENU_BACK,
    MENU_COUNT
  };
  MenuOption selectedMenu;

  const int LOGS_PER_PAGE = 8;  // 페이지당 표시할 로그 수

public:
  LogScreen(TFT_eSPI* display) :
    tft(display),
    lastUpdate(0),
    needsFullRedraw(true),
    logCount(0),
    scrollPosition(0),
    selectedIndex(0),
    currentFilter(FILTER_ALL),
    currentSort(SORT_NEWEST),
    showingMenu(false),
    selectedMenu(MENU_FILTER) {
    // 테스트 로그 추가
    addTestLogs();
  }

  // 초기화
  void begin() {
    scrollPosition = 0;
    selectedIndex = 0;
    showingMenu = false;
    needsFullRedraw = true;
    draw();
  }

  // 전체 화면 그리기
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
    if (showingMenu) {
      // 메뉴에서 항목 선택
      int newMenu = (int)selectedMenu + direction;
      if (newMenu < 0) newMenu = MENU_COUNT - 1;
      if (newMenu >= MENU_COUNT) newMenu = 0;
      selectedMenu = (MenuOption)newMenu;
    } else {
      // 로그 목록 스크롤
      int filteredCount = getFilteredLogCount();
      if (filteredCount > 0) {
        scrollPosition += direction;
        if (scrollPosition < 0) scrollPosition = 0;
        if (scrollPosition >= filteredCount) scrollPosition = filteredCount - 1;
      }
    }
    needsFullRedraw = true;
  }

  // 버튼 클릭 처리
  void onButtonClick() {
    if (showingMenu) {
      handleMenuSelection();
    } else {
      // 메뉴 열기
      showingMenu = true;
      selectedMenu = MENU_FILTER;
    }
    needsFullRedraw = true;
  }

  // 로그 추가
  void addLog(LogType type, const char* message) {
    if (logCount >= MAX_LOG_ENTRIES) {
      // 오래된 로그 삭제 (순환 버퍼)
      for (int i = 0; i < MAX_LOG_ENTRIES - 1; i++) {
        logs[i] = logs[i + 1];
      }
      logCount = MAX_LOG_ENTRIES - 1;
    }

    logs[logCount].type = type;
    strncpy(logs[logCount].message, message, 63);
    logs[logCount].message[63] = '\0';
    logs[logCount].timestamp = millis();
    logs[logCount].read = false;
    logCount++;
  }

  // 모든 로그 삭제
  void clearAllLogs() {
    logCount = 0;
    scrollPosition = 0;
    Serial.println("All logs cleared");
  }

  // 읽지 않은 로그 수
  int getUnreadCount() {
    int count = 0;
    for (int i = 0; i < logCount; i++) {
      if (!logs[i].read) count++;
    }
    return count;
  }

private:
  // 헤더
  void drawHeader() {
    tft->fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_HEADER_BG);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextSize(2);
    tft->setCursor(10, 8);
    tft->print("LOGS & ALERTS");

    // 읽지 않은 로그 수
    int unread = getUnreadCount();
    if (unread > 0) {
      tft->setTextSize(1);
      tft->setCursor(220, 12);
      tft->setTextColor(COLOR_WARNING);
      tft->printf("(%d new)", unread);
    }
  }

  // 로그 목록 그리기
  void drawLogList() {
    int y = HEADER_HEIGHT + 5;
    int lineHeight = 22;
    int displayCount = 0;

    // 필터 정보 표시
    tft->setTextSize(1);
    tft->setTextColor(0x07FF);
    tft->setCursor(5, y);
    tft->printf("Filter: %s | Sort: %s",
               getFilterName(currentFilter),
               currentSort == SORT_NEWEST ? "Newest" : "Oldest");
    y += lineHeight;

    // 필터링 및 정렬된 로그 가져오기
    int filteredIndices[MAX_LOG_ENTRIES];
    int filteredCount = getFilteredAndSortedLogs(filteredIndices);

    if (filteredCount == 0) {
      tft->setTextColor(COLOR_INACTIVE);
      tft->setCursor(80, SCREEN_HEIGHT / 2);
      tft->print("No logs to display");
      return;
    }

    // 스크롤 범위 체크
    if (scrollPosition >= filteredCount) {
      scrollPosition = filteredCount - 1;
    }

    // 로그 항목 표시
    int startIdx = scrollPosition;
    int endIdx = min(startIdx + LOGS_PER_PAGE, filteredCount);

    for (int i = startIdx; i < endIdx && displayCount < LOGS_PER_PAGE; i++, displayCount++) {
      int logIdx = filteredIndices[i];
      drawLogEntry(5, y, logIdx, i == scrollPosition);
      y += lineHeight;
    }

    // 스크롤 인디케이터
    if (filteredCount > LOGS_PER_PAGE) {
      drawScrollIndicator(filteredCount);
    }
  }

  // 로그 항목 그리기
  void drawLogEntry(int x, int y, int logIdx, bool selected) {
    LogEntry& log = logs[logIdx];

    // 선택 표시
    if (selected) {
      tft->fillRect(x - 2, y - 2, SCREEN_WIDTH - 10, 20, COLOR_PANEL_BG);
      tft->drawRect(x - 2, y - 2, SCREEN_WIDTH - 10, 20, COLOR_WARNING);
    }

    // 타입 아이콘 및 색상
    uint16_t typeColor;
    const char* typeIcon;
    switch (log.type) {
      case LOG_INFO:
        typeColor = COLOR_OK;
        typeIcon = "[I]";
        break;
      case LOG_WARNING:
        typeColor = COLOR_WARNING;
        typeIcon = "[W]";
        break;
      case LOG_ERROR:
        typeColor = COLOR_ERROR;
        typeIcon = "[E]";
        break;
      case LOG_EVENT:
        typeColor = 0x07FF;  // 청록색
        typeIcon = "[*]";
        break;
      default:
        typeColor = COLOR_TEXT;
        typeIcon = "[?]";
        break;
    }

    tft->setTextSize(1);
    tft->setTextColor(typeColor);
    tft->setCursor(x, y);
    tft->print(typeIcon);

    // 메시지
    tft->setTextColor(log.read ? COLOR_INACTIVE : COLOR_TEXT);
    tft->setCursor(x + 25, y);

    // 메시지 길이 제한 (약 40자)
    char displayMsg[41];
    strncpy(displayMsg, log.message, 40);
    displayMsg[40] = '\0';
    tft->print(displayMsg);

    // 시간 (우측)
    tft->setTextColor(COLOR_INACTIVE);
    tft->setTextSize(1);
    unsigned long timeSince = (millis() - log.timestamp) / 1000 / 60;  // 분
    if (timeSince < 60) {
      tft->setCursor(280, y);
      tft->printf("%lum", timeSince);
    } else {
      tft->setCursor(280, y);
      tft->printf("%luh", timeSince / 60);
    }

    // 선택된 항목은 읽음 처리
    if (selected) {
      log.read = true;
    }
  }

  // 스크롤 인디케이터
  void drawScrollIndicator(int totalCount) {
    int barX = SCREEN_WIDTH - 8;
    int barY = HEADER_HEIGHT + 30;
    int barH = SCREEN_HEIGHT - HEADER_HEIGHT - STATUS_BAR_HEIGHT - 40;

    // 배경
    tft->drawRect(barX, barY, 5, barH, COLOR_TEXT);

    // 스크롤 위치
    int indicatorH = max(10, barH * LOGS_PER_PAGE / totalCount);
    int indicatorY = barY + (barH - indicatorH) * scrollPosition / max(1, totalCount - LOGS_PER_PAGE);
    tft->fillRect(barX + 1, indicatorY, 3, indicatorH, COLOR_OK);
  }

  // 메뉴 그리기
  void drawMenu() {
    int menuWidth = 200;
    int menuHeight = 150;
    int menuX = (SCREEN_WIDTH - menuWidth) / 2;
    int menuY = (SCREEN_HEIGHT - menuHeight) / 2;

    // 메뉴 배경
    tft->fillRect(menuX, menuY, menuWidth, menuHeight, COLOR_PANEL_BG);
    tft->drawRect(menuX, menuY, menuWidth, menuHeight, COLOR_TEXT);
    tft->drawRect(menuX + 1, menuY + 1, menuWidth - 2, menuHeight - 2, COLOR_TEXT);

    // 메뉴 제목
    tft->setTextSize(1);
    tft->setTextColor(0x07FF);
    tft->setCursor(menuX + 10, menuY + 10);
    tft->print("MENU");

    int itemY = menuY + 30;
    int itemHeight = 25;

    // Filter
    drawMenuItem(menuX + 10, itemY, menuWidth - 20, itemHeight,
                 "Filter", getFilterName(currentFilter),
                 selectedMenu == MENU_FILTER);
    itemY += itemHeight + 5;

    // Sort
    drawMenuItem(menuX + 10, itemY, menuWidth - 20, itemHeight,
                 "Sort", currentSort == SORT_NEWEST ? "Newest" : "Oldest",
                 selectedMenu == MENU_SORT);
    itemY += itemHeight + 5;

    // Clear All
    drawMenuItem(menuX + 10, itemY, menuWidth - 20, itemHeight,
                 "Clear All", "",
                 selectedMenu == MENU_CLEAR_ALL);
    itemY += itemHeight + 5;

    // Back
    drawMenuItem(menuX + 10, itemY, menuWidth - 20, itemHeight,
                 "Back", "",
                 selectedMenu == MENU_BACK);
  }

  // 메뉴 항목 그리기
  void drawMenuItem(int x, int y, int w, int h, const char* label, const char* value, bool selected) {
    if (selected) {
      tft->fillRect(x, y, w, h, 0x18E3);
      tft->drawRect(x, y, w, h, COLOR_WARNING);
    } else {
      tft->drawRect(x, y, w, h, COLOR_TEXT);
    }

    tft->setTextSize(1);
    tft->setTextColor(COLOR_TEXT);
    tft->setCursor(x + 5, y + 5);
    tft->print(label);

    if (strlen(value) > 0) {
      tft->setTextColor(COLOR_OK);
      tft->setCursor(x + 5, y + 15);
      tft->print(value);
    }
  }

  // 메뉴 선택 처리
  void handleMenuSelection() {
    switch (selectedMenu) {
      case MENU_FILTER:
        // 필터 순환
        currentFilter = (LogFilter)((currentFilter + 1) % FILTER_COUNT);
        scrollPosition = 0;
        Serial.printf("Filter changed to: %s\n", getFilterName(currentFilter));
        break;

      case MENU_SORT:
        // 정렬 토글
        currentSort = (currentSort == SORT_NEWEST) ? SORT_OLDEST : SORT_NEWEST;
        scrollPosition = 0;
        Serial.printf("Sort changed to: %s\n", currentSort == SORT_NEWEST ? "Newest" : "Oldest");
        break;

      case MENU_CLEAR_ALL:
        // 전체 삭제
        clearAllLogs();
        showingMenu = false;
        break;

      case MENU_BACK:
        // 메뉴 닫기
        showingMenu = false;
        break;
    }
  }

  // 푸터
  void drawFooter() {
    int y = SCREEN_HEIGHT - STATUS_BAR_HEIGHT;
    tft->fillRect(0, y, SCREEN_WIDTH, STATUS_BAR_HEIGHT, COLOR_PANEL_BG);

    tft->setTextSize(1);
    tft->setTextColor(COLOR_TEXT);
    tft->setCursor(5, y + 5);

    if (showingMenu) {
      tft->print("Rotate:Select | Click:Apply");
    } else {
      tft->print("Rotate:Scroll | Click:Menu");
    }
  }

  // 필터링 및 정렬된 로그 인덱스 가져오기
  int getFilteredAndSortedLogs(int* indices) {
    int count = 0;

    // 필터링
    for (int i = 0; i < logCount; i++) {
      if (currentFilter == FILTER_ALL || logs[i].type == (LogType)(currentFilter - 1)) {
        indices[count++] = i;
      }
    }

    // 정렬
    if (currentSort == SORT_OLDEST) {
      // 오래된 순 (이미 순서대로 저장되어 있음)
      // 아무것도 하지 않음
    } else {
      // 최신순 (역순)
      for (int i = 0; i < count / 2; i++) {
        int temp = indices[i];
        indices[i] = indices[count - 1 - i];
        indices[count - 1 - i] = temp;
      }
    }

    return count;
  }

  // 필터링된 로그 수
  int getFilteredLogCount() {
    int indices[MAX_LOG_ENTRIES];
    return getFilteredAndSortedLogs(indices);
  }

  // 필터 이름
  const char* getFilterName(LogFilter filter) {
    switch (filter) {
      case FILTER_ALL: return "All";
      case FILTER_INFO: return "Info";
      case FILTER_WARNING: return "Warning";
      case FILTER_ERROR: return "Error";
      case FILTER_EVENT: return "Event";
      default: return "Unknown";
    }
  }

  // 테스트 로그 추가
  void addTestLogs() {
    addLog(LOG_EVENT, "System started");
    addLog(LOG_INFO, "Tank module connected");
    addLog(LOG_INFO, "GrowBox module connected");
    addLog(LOG_INFO, "Nutrient module connected");
    addLog(LOG_INFO, "Feeder module connected");
    addLog(LOG_WARNING, "Tank water level low");
    addLog(LOG_ERROR, "GrowBox leak detected!");
    addLog(LOG_EVENT, "Feeding completed");
    addLog(LOG_WARNING, "Feeder food level low");
    addLog(LOG_INFO, "Nutrient supply started");
    addLog(LOG_EVENT, "LED schedule activated");
    addLog(LOG_WARNING, "Tank pH out of range");
    addLog(LOG_ERROR, "Nutrient supply failed");
    addLog(LOG_INFO, "System OK");
    addLog(LOG_EVENT, "Auto feeding at 08:00");
  }
};

#endif // LOG_SCREEN_H
