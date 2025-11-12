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

  // 시스템 상태에 따라 LED 자동 업데이트
  void updateFromStatus(const SystemStatus& status) {
    // Blue: 서버 연결
    setBlue(status.serverConnected);

    // Green: 모든 모듈 정상
    setGreen(status.modulesOK);

    // Red: 경고 또는 오류 발생
    setRed(status.hasWarning || status.hasError);
  }

  // 상태 가져오기
  bool getBlue() const { return blueState; }
  bool getGreen() const { return greenState; }
  bool getRed() const { return redState; }

  // 모든 LED OFF
  void allOff() {
    setBlue(false);
    setGreen(false);
    setRed(false);
  }

  // 시작 시퀀스 (테스트용)
  void startupSequence() {
    allOff();
    delay(200);
    setBlue(true);
    delay(200);
    setGreen(true);
    delay(200);
    setRed(true);
    delay(200);
    allOff();
    delay(200);
  }
};

#endif // LED_CONTROLLER_H
