#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ==================== FreeRTOS/Task 설정 ====================
#define TASK_CAN_CORE 0
#define TASK_UI_CORE 1
#define TASK_UART_CORE 0
#define TASK_SCHEDULER_CORE 0

#define TASK_CAN_PRIORITY 3
#define TASK_UI_PRIORITY 2
#define TASK_UART_PRIORITY 1
#define TASK_SCHEDULER_PRIORITY 1

#define TASK_CAN_STACK_SIZE 4096
#define TASK_UI_STACK_SIZE 8192
#define TASK_UART_STACK_SIZE 4096
#define TASK_SCHEDULER_STACK_SIZE 4096

// ==================== 핀 설정 ====================
#define ENCODER_CLK 32
#define ENCODER_DT 33
#define ENCODER_SW 26
#define LED_BLUE 25
#define LED_GREEN 27
#define LED_RED 14
#define CAN_TX 5
#define CAN_RX 4

// ==================== CAN 프로토콜 정의 (main_controller.py 참조) ====================
enum ModuleID {
    MAIN = 0x01,
    TANK = 0x10,
    GROW = 0x20,
    NUTRI = 0x30,
    FEED = 0x40,
};

enum Cmd {
    SENS = 0x01,
    STAT = 0x02,
    CMD = 0x10,
    ACK = 0x11,
    ERR = 0x12,
};

// ==================== 타이밍 ====================
#define CAN_PERIOD_MS 100
#define UI_PERIOD_MS 200
#define UART_PERIOD_MS 200
#define SCHEDULER_PERIOD_MS 1000
#define CAN_WATCHDOG_TIMEOUT_MS 500

// ==================== 화면 설정 ====================
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define HEADER_HEIGHT 30
#define STATUS_BAR_HEIGHT 20

// ==================== 색상 정의 ====================
#define COLOR_BACKGROUND 0x0000
#define COLOR_TEXT 0xFFFF
#define COLOR_HEADER_BG 0x001F
#define COLOR_PANEL_BG 0x18E3
#define COLOR_OK 0x07E0
#define COLOR_WARNING 0xFFE0
#define COLOR_ERROR 0xF800
#define COLOR_INACTIVE 0x7BEF
#define COLOR_LED_BLUE 0x001F
#define COLOR_LED_GREEN 0x07E0
#define COLOR_LED_RED 0xF800

// ==================== 화면 ID ====================
enum ScreenID {
    SCREEN_DASHBOARD = 0,
    SCREEN_TANK,
    SCREEN_GROWBOX,
    SCREEN_NUTRIENT,
    SCREEN_FEEDER,
    SCREEN_LOG,
    SCREEN_SETTINGS,
    SCREEN_COUNT
};

// ==================== 중앙 데이터 구조체 (main_controller.py 참조) ====================

// --- 개별 모듈 상태 ---
struct TankState {
    float temp;
    float level;
    float ph;
    int tds;
    float turb;
    float dissolvedOxygen;
};

struct GrowState {
    float temp;
    float hum;
    uint8_t leak_bits; // 4-bit
    int led;
};

struct NutriState {
    struct { int A, B, C, D; } ratio;
    struct { int A, B, C, D; } remain_ml;
};

struct FeedState {
    int remain_g;
};

// --- 모듈 통신 상태 ---
struct ModuleCommState {
    unsigned long last_ts_ms;
    bool ok;
};

// --- 전체 시스템 상태 ---
struct SystemState {
    TankState tank;
    GrowState grow;
    NutriState nutri;
    FeedState feed;

    struct {
        ModuleCommState tank;
        ModuleCommState grow;
        ModuleCommState nutri;
        ModuleCommState feed;
    } comm;

    bool serverConnected;
};

// --- LED 상태 ---
struct LEDState {
    bool blue;  // server link
    bool green; // modules ok
    bool red;   // alarm
};

// --- 알람 상태 ---
#define MAX_ACTIVE_ALARMS 5
#define ALARM_CODE_MAX_LEN 16
#define ALARM_MSG_MAX_LEN 48

struct Alarm {
    char code[ALARM_CODE_MAX_LEN];
    char msg[ALARM_MSG_MAX_LEN];
    bool sticky;
    unsigned long raised_at_ms;
};

struct AlarmState {
    Alarm active_alarms[MAX_ACTIVE_ALARMS];
    int count;
};


// ==================== 전역 상태 변수 및 뮤텍스 (외부 선언) ====================
// 이 변수들의 실제 정의는 .ino 파일에 있습니다.
extern SystemState g_systemState;
extern LEDState g_ledState;
extern AlarmState g_alarmState;
extern SemaphoreHandle_t g_stateMutex;


#endif // CONFIG_H
