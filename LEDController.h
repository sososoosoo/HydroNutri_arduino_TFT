#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Arduino.h>
#include "Config.h"

class LEDController {
private:
  bool blueState;
  bool greenState;
  bool redState;

public:
  LEDController() : blueState(false), greenState(false), redState(false) {}

  // LED 초기화
  void begin() {
    pinMode(LED_BLUE, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_RED, OUTPUT);

    // 초기 상태: 모두 OFF
    setBlue(false);
    setGreen(false);
    setRed(false);
  }

  // Blue LED 제어 (서버 연결 상태)
  void setBlue(bool state) {
    blueState = state;
    digitalWrite(LED_BLUE, state ? HIGH : LOW);
  }

  // Green LED 제어 (모듈 통신 정상)
  void setGreen(bool state) {
    greenState = state;
    digitalWrite(LED_GREEN, state ? HIGH : LOW);
  }

  // Red LED 제어 (경고/오류)
  void setRed(bool state) {
    redState = state;
    digitalWrite(LED_RED, state ? HIGH : LOW);
  }

  void startupSequence() {
    // ... (implementation)
  }
};

#endif // LED_CONTROLLER_H
