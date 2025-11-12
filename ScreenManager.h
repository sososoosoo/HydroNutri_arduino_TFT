#ifndef SCREEN_MANAGER_H
#define SCREEN_MANAGER_H

#include "Config.h"

class ScreenManager {
private:
  ScreenID currentScreen;
  ScreenID previousScreen;
  bool needsRedraw;

public:
  ScreenManager() : currentScreen(SCREEN_DASHBOARD), previousScreen(SCREEN_DASHBOARD), needsRedraw(true) {}

  // 현재 화면 가져오기
  ScreenID getCurrentScreen() const {
    return currentScreen;
  }

  // 이전 화면 가져오기
  ScreenID getPreviousScreen() const {
    return previousScreen;
  }

  // 화면 전환
  void setScreen(ScreenID newScreen) {
    if (newScreen != currentScreen) {
      previousScreen = currentScreen;
      currentScreen = newScreen;
      needsRedraw = true;
      Serial.printf("Screen changed: %d -> %d\n", previousScreen, currentScreen);
    }
  }

  // 다음 화면으로 이동
  void nextScreen() {
    int next = (int)currentScreen + 1;
    if (next >= SCREEN_COUNT) {
      next = 0;
    }
    setScreen((ScreenID)next);
  }

  // 이전 화면으로 이동
  void prevScreen() {
    int prev = (int)currentScreen - 1;
    if (prev < 0) {
      prev = SCREEN_COUNT - 1;
    }
    setScreen((ScreenID)prev);
  }

  // Dashboard로 돌아가기
  void goToDashboard() {
    setScreen(SCREEN_DASHBOARD);
  }

  // 재그리기 필요 여부
  bool needsFullRedraw() {
    if (needsRedraw) {
      needsRedraw = false;
      return true;
    }
    return false;
  }

  // 화면 이름 가져오기
  const char* getScreenName(ScreenID screen) {
    switch (screen) {
      case SCREEN_DASHBOARD: return "Dashboard";
      case SCREEN_TANK: return "Tank Detail";
      case SCREEN_GROWBOX: return "GrowBox Detail";
      case SCREEN_NUTRIENT: return "Nutrient Detail";
      case SCREEN_FEEDER: return "Feeder Detail";
      case SCREEN_LOG: return "Log/Alerts";
      case SCREEN_SETTINGS: return "Settings";
      default: return "Unknown";
    }
  }

  const char* getCurrentScreenName() {
    return getScreenName(currentScreen);
  }
};

#endif // SCREEN_MANAGER_H
