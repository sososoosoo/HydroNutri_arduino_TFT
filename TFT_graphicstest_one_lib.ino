/*
 * Smart Farm Display Controller - Refactored for FreeRTOS
 *
 * This firmware is structured around a multitasking architecture using FreeRTOS
 * to handle concurrent operations like CAN communication, UI updates, and background tasks.
 *
 * Core Tasks:
 * - uiTask: Manages the TFT display, screen transitions, and user input (rotary encoder).
 * - canTask: Handles receiving and parsing of CAN messages from other modules.
 * - canWatchdogTask: Monitors communication health of all modules.
 * - uartTask: Manages communication with the server (e.g., Raspberry Pi).
 * - schedulerTask: Runs time-based events like feeding or lighting schedules.
 */

#include "SPI.h"
#include "TFT_eSPI.h"
#include "Config.h"
#include "LEDController.h"
#include "ScreenManager.h"
#include "DashboardScreen.h"
#include "TankScreen.h"
#include "GrowBoxScreen.h"
#include "NutrientScreen.h"
#include "FeederScreen.h"
#include "LogScreen.h"
#include "SettingsScreen.h"
#include "CANHandler.h"
#include "AlarmManager.h"
#include "Persistence.h"

// ==================== 전역 객체 및 상태 변수 정의 ====================
// Config.h에 extern으로 선언된 변수들의 실제 정의
SystemState g_systemState;
LEDState g_ledState;
AlarmState g_alarmState;
SystemSettings g_settings;
SemaphoreHandle_t g_stateMutex;

// FreeRTOS Task 핸들
TaskHandle_t g_uiTaskHandle;
TaskHandle_t g_canTaskHandle;
TaskHandle_t g_canWatchdogTaskHandle;
TaskHandle_t g_uartTaskHandle;
TaskHandle_t g_schedulerTaskHandle;

// 하드웨어 및 화면 객체
TFT_eSPI tft = TFT_eSPI();
LEDController ledController;
ScreenManager screenManager;
CANHandler canHandler;

// 화면 객체 (TODO: 새로운 데이터 구조를 받도록 수정 필요)
DashboardScreen* dashboard;
TankScreen* tankScreen;
GrowBoxScreen* growBoxScreen;
NutrientScreen* nutrientScreen;
FeederScreen* feederScreen;
LogScreen* logScreen;
SettingsScreen* settingsScreen;

// ==================== Task 함수 프로토타입 ====================
void uiTask(void *pvParameters);
void canTask(void *pvParameters);
void canWatchdogTask(void *pvParameters);
void uartTask(void *pvParameters);
void schedulerTask(void *pvParameters);

// ==================== 데이터 초기화 ====================
void initSystemData() {
    // g_stateMutex를 사용하여 데이터 접근을 보호해야 하지만,
    // 초기화 단계에서는 아직 다른 Task가 없으므로 직접 접근합니다.
    memset(&g_systemState, 0, sizeof(SystemState));
    memset(&g_ledState, 0, sizeof(LEDState));
    memset(&g_alarmState, 0, sizeof(AlarmState));
    
    g_systemState.serverConnected = false;

    // Initialize settings with default values first
    memset(&g_settings, 0, sizeof(SystemSettings));
    strcpy(g_settings.fw_version, "0.1.0");
    g_settings.time_sync_from_server = true;
    for(int i=0; i<4; ++i) g_settings.module_enable[i] = true;

    g_settings.feed_schedule[0] = {7, 30, 5, true};
    g_settings.feed_schedule[1] = {12, 0, 5, true};
    g_settings.feed_schedule[2] = {18, 30, 5, true};

    g_settings.grow_led_schedule = {8, 0, 22, 0, true};
    g_settings.grow_led_brightness = 80;

    // Then, try to load settings from NVS
    loadSettings(g_settings);
}

// ==================== Setup ====================
void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== Smart Farm Display Controller (FreeRTOS) ===");

    // Initialize NVS
    init_nvs();

    // 뮤텍스 생성
    g_stateMutex = xSemaphoreCreateMutex();
    if (g_stateMutex == NULL) {
        Serial.println("Error: Failed to create state mutex!");
        // 무한 루프로 시스템 정지
        while(1);
    }

    // 데이터 초기화
    initSystemData();

    // 하드웨어 초기화
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(COLOR_BACKGROUND);

    ledController.begin();
    ledController.startupSequence();

    // CAN 초기화
    if (!canHandler.begin()) {
      Serial.println("CAN init failed - running without CAN");
    }

    // 화면 객체 생성 및 상태 참조 설정
    dashboard = new DashboardScreen(&tft);
    dashboard->setStateReferences(&g_systemState, &g_ledState, &g_alarmState);

    tankScreen = new TankScreen(&tft);
    tankScreen->setStateReference(&g_systemState);
    tankScreen->setCANHandler(&canHandler);

    growBoxScreen = new GrowBoxScreen(&tft);
    growBoxScreen->setStateReference(&g_systemState);

    nutrientScreen = new NutrientScreen(&tft);
    nutrientScreen->setStateReference(&g_systemState);

    feederScreen = new FeederScreen(&tft);
    feederScreen->setStateReference(&g_systemState);

        logScreen = new LogScreen(&tft);

        logScreen->setStateReference(&g_alarmState);

    

        settingsScreen = new SettingsScreen(&tft);

        settingsScreen->setSettingsReference(&g_settings);

    

        Serial.println("Hardware and screens initialized.");

    

        // FreeRTOS Task 생성

        xTaskCreatePinnedToCore(uiTask, "UITask", TASK_UI_STACK_SIZE, NULL, TASK_UI_PRIORITY, &g_uiTaskHandle, TASK_UI_CORE);

        xTaskCreatePinnedToCore(canTask, "CANTask", TASK_CAN_STACK_SIZE, NULL, TASK_CAN_PRIORITY, &g_canTaskHandle, TASK_CAN_CORE);

        xTaskCreatePinnedToCore(canWatchdogTask, "CANWatchdogTask", 2048, NULL, TASK_CAN_PRIORITY, &g_canWatchdogTaskHandle, TASK_CAN_CORE);

        xTaskCreatePinnedToCore(uartTask, "UARTTask", TASK_UART_STACK_SIZE, NULL, TASK_UART_PRIORITY, &g_uartTaskHandle, TASK_UART_CORE);

        xTaskCreatePinnedToCore(schedulerTask, "SchedulerTask", TASK_SCHEDULER_STACK_SIZE, NULL, TASK_SCHEDULER_PRIORITY, &g_schedulerTaskHandle, TASK_SCHEDULER_CORE);

    

        Serial.println("All tasks created. System is running.");

    

        // setup() 완료 후 vTaskStartScheduler()가 자동으로 호출됨.

        // loop() Task는 더 이상 주도적인 역할을 하지 않음.

    }

    

    // ==================== 메인 루프 (FreeRTOS에서는 거의 사용 안 함) ====================

    void loop() {

        // 모든 작업은 개별 Task에서 처리되므로 loop는 비워두거나 최소한의 작업만 수행

        vTaskDelay(pdMS_TO_TICKS(1000));

    }

    

    // ==================== UI Task ====================

    // 화면 갱신, 로터리 엔코더 및 버튼 입력 처리

    void uiTask(void *pvParameters) {

        Serial.println("UI Task started.");

    

        // 로터리 엔코더 관련 변수

        int lastEncoderValue = 0;

        int encoderValue = 0;

        unsigned long lastEncoderTime = 0;

        bool buttonPressed = false;

        unsigned long lastButtonTime = 0;

        

        // 로터리 엔코더 초기화

        pinMode(ENCODER_CLK, INPUT_PULLUP);

        pinMode(ENCODER_DT, INPUT_PULLUP);

        pinMode(ENCODER_SW, INPUT_PULLUP);

    

        // 인터럽트 핸들러 (람다 함수 사용)

        attachInterrupt(digitalPinToInterrupt(ENCODER_CLK), [&]() {

            if (millis() - lastEncoderTime < 3) return;

            lastEncoderTime = millis();

            if (digitalRead(ENCODER_DT) != digitalRead(ENCODER_CLK)) {

                encoderValue++;

            } else {

                encoderValue--;

            }

        }, CHANGE);

    

        // 초기 화면 그리기를 위해 플래그 설정

        screenManager.needsFullRedraw();

    

        for (;;) {

                    // --- 입력 처리 ---

                    int direction = 0;

                    if (encoderValue != lastEncoderValue) {

                        direction = (encoderValue > lastEncoderValue) ? 1 : -1;

                        lastEncoderValue = encoderValue;

                    }

            

                    bool clicked = false;

                    if (digitalRead(ENCODER_SW) == LOW) {

                        if (!buttonPressed && (millis() - lastButtonTime > 250)) {

                            buttonPressed = true;

                            lastButtonTime = millis();

                            clicked = true;

                        }

                    } else {

                        buttonPressed = false;

                    }

            

                    // --- 현재 화면에 입력 전달 ---

                    switch (screenManager.getCurrentScreen()) {

                        case SCREEN_DASHBOARD:

                            if (direction != 0) { // Dashboard에서는 화면 전환

                                if (direction > 0) screenManager.nextScreen();

                                else screenManager.prevScreen();

                            }

                            if (clicked) { // Dashboard에서 클릭 시 Tank 화면으로 이동 (예시)

                                screenManager.setScreen(SCREEN_TANK);

                            }

                            break;

                        

                        case SCREEN_TANK:

                            if (direction != 0) tankScreen->onEncoderRotate(direction);

                            if (clicked) tankScreen->onButtonClick();

                            // BACK 버튼 처리

                            if (clicked && tankScreen->getSelectedItem() == TANK_CONTROL_BACK && !tankScreen->isEditMode()) {

                                screenManager.goToDashboard();

                            }

                            break;

            

                        case SCREEN_GROWBOX:

                            if (direction != 0) growBoxScreen->onEncoderRotate(direction);

                            if (clicked) growBoxScreen->onButtonClick();

                            if (clicked && growBoxScreen->getSelectedItem() == GROWBOX_CONTROL_BACK && !growBoxScreen->isEditMode()) {

                                screenManager.goToDashboard();

                            }

                            break;

            

                        case SCREEN_NUTRIENT:

                            if (direction != 0) nutrientScreen->onEncoderRotate(direction);

                            if (clicked) nutrientScreen->onButtonClick();

                            if (clicked && nutrientScreen->getSelectedItem() == NUTRIENT_CONTROL_BACK) {

                                screenManager.goToDashboard();

                            }

                            break;

            

                        case SCREEN_FEEDER:

                            if (direction != 0) feederScreen->onEncoderRotate(direction);

                            if (clicked) feederScreen->onButtonClick();

                             if (clicked && feederScreen->getSelectedItem() == FEEDER_CONTROL_BACK && !feederScreen->isEditMode()) {

                                screenManager.goToDashboard();

                            }

                            break;

            

                        case SCREEN_LOG:

                            if (direction != 0) logScreen->onEncoderRotate(direction);

                            if (clicked) logScreen->onButtonClick();

                            break;

            

                        case SCREEN_SETTINGS:

                            if (direction != 0) settingsScreen->onEncoderRotate(direction);

                            if (clicked) settingsScreen->onButtonClick();

                            // if (clicked && settingsScreen->getSelectedItem() == SETTINGS_BACK) {

                            //     screenManager.goToDashboard();

                            // }

                            break;

            

                        default:

                            if (direction != 0) {

                                if (direction > 0) screenManager.nextScreen();

                                else screenManager.prevScreen();

                            }

                            break;

                    }

                    

                    // --- 상태에 따른 LED 업데이트 ---

            if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {

                ledController.setBlue(g_ledState.blue);

                ledController.setGreen(g_ledState.green);

                ledController.setRed(g_ledState.red);

                xSemaphoreGive(g_stateMutex);

            }

            ledController.update();

    

    

            // --- 현재 활성화된 화면 업데이트 ---

            bool needsRedraw = screenManager.needsFullRedraw();

    

            switch (screenManager.getCurrentScreen()) {

                case SCREEN_DASHBOARD:

                    if (needsRedraw) dashboard->begin();

                    dashboard->update();

                    break;

                

                case SCREEN_TANK:

                    if (needsRedraw) tankScreen->begin();

                    tankScreen->update();

                    break;

    

                case SCREEN_GROWBOX:

                    if (needsRedraw) growBoxScreen->begin();

                    growBoxScreen->update();

                    break;

    

                case SCREEN_NUTRIENT:

                    if (needsRedraw) nutrientScreen->begin();

                    nutrientScreen->update();

                    break;

    

                case SCREEN_FEEDER:

                    if (needsRedraw) feederScreen->begin();

                    feederScreen->update();

                    break;

    

                case SCREEN_LOG:

                    if (needsRedraw) logScreen->begin();

                    logScreen->update();

                    break;

                

                case SCREEN_SETTINGS:

                    if (needsRedraw) settingsScreen->begin();

                    settingsScreen->update();

                    break;

    

                // 다른 화면들도 위와 같이 임시 처리

                default:

                    if (needsRedraw) {

                        tft.fillScreen(COLOR_BACKGROUND);

                        tft.setTextColor(COLOR_TEXT);

                        tft.setTextDatum(MC_DATUM);

                        const char* screenName = screenManager.getCurrentScreenName();

                        tft.drawString(screenName, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);

                    }

                    break;

            }

    

            // Task 주기

            vTaskDelay(pdMS_TO_TICKS(50)); // UI 반응성을 위해 딜레이를 줄임

        }

    }

// ==================== CAN Task ====================
// CAN 메시지 수신 및 파싱
void canTask(void *pvParameters) {
    Serial.println("CAN Task started.");
    twai_message_t rx_message;

    for (;;) {
        // CAN 메시지 수신
        if (canHandler.receiveMessage(rx_message)) {
            // g_stateMutex를 사용하여 공유 데이터인 g_systemState를 안전하게 업데이트
            if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                // 수신된 프레임을 파싱하여 g_systemState 업데이트
                canHandler.parseFrame(rx_message, g_systemState);

                // --- 파싱된 데이터 기반 알람 처리 ---
                // 예: 재배기 누수 감지 알람
                if (rx_message.identifier == GROW) {
                    if (g_systemState.grow.leak_bits > 0) {
                        raise_alarm("E-LEAK", "Leak detected!");
                    } else {
                        clear_alarm("E-LEAK");
                    }
                }
                // TODO: 다른 센서 데이터 기반 알람들 추가 (예: 양액 부족, 먹이 부족)
                
                // 뮤텍스 해제
                xSemaphoreGive(g_stateMutex);
            } else {
                Serial.println("CAN Task: Failed to take mutex.");
            }
        }

        // Task 주기
        vTaskDelay(pdMS_TO_TICKS(CAN_PERIOD_MS));
    }
}

// ==================== CAN Watchdog Task ====================
// 모듈 통신 상태 감시
void canWatchdogTask(void *pvParameters) {
    Serial.println("CAN Watchdog Task started.");
    for (;;) {
        if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            bool all_modules_ok = true;
            unsigned long now = millis();

            // Tank Module
            if (now - g_systemState.comm.tank.last_ts_ms > CAN_WATCHDOG_TIMEOUT_MS) {
                if (g_systemState.comm.tank.ok) { // Only log change
                    g_systemState.comm.tank.ok = false;
                }
                all_modules_ok = false;
            }

            // Grow Module
            if (now - g_systemState.comm.grow.last_ts_ms > CAN_WATCHDOG_TIMEOUT_MS) {
                if (g_systemState.comm.grow.ok) {
                    g_systemState.comm.grow.ok = false;
                }
                all_modules_ok = false;
            }

            // Nutrient Module
            if (now - g_systemState.comm.nutri.last_ts_ms > CAN_WATCHDOG_TIMEOUT_MS) {
                if (g_systemState.comm.nutri.ok) {
                    g_systemState.comm.nutri.ok = false;
                }
                all_modules_ok = false;
            }

            // Feeder Module
            if (now - g_systemState.comm.feed.last_ts_ms > CAN_WATCHDOG_TIMEOUT_MS) {
                if (g_systemState.comm.feed.ok) {
                    g_systemState.comm.feed.ok = false;
                }
                all_modules_ok = false;
            }

            // Update LED and Alarm states
            g_ledState.green = all_modules_ok;
            if (!all_modules_ok) {
                raise_alarm("E-CAN-LOST", "Module comm lost");
            } else {
                clear_alarm("E-CAN-LOST");
            }
            
            xSemaphoreGive(g_stateMutex);
        } else {
            Serial.println("Watchdog: Failed to take mutex.");
        }
        
        // Task 주기
        vTaskDelay(pdMS_TO_TICKS(CAN_WATCHDOG_TIMEOUT_MS));
    }
}

// ==================== UART Task ====================
// 서버(RPI)와 데이터 송수신

// CRC-16/X.25
uint16_t crc16_hqx(const uint8_t *data, uint16_t length) {
    uint16_t crc = 0xFFFF;
    while (length--) {
        crc ^= *data++;
        for (uint8_t i = 0; i < 8; i++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0x8408;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

void uartTask(void *pvParameters) {
    Serial.println("UART Task started.");
    static bool serverSimConnected = true; // 시뮬레이션용 서버 연결 상태

    for (;;) {
        // --- 상태 전송 ---
        if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            g_systemState.serverConnected = serverSimConnected;
            g_ledState.blue = serverSimConnected;

            if (serverSimConnected) {
                // 1. Create JSON string manually
                String json = "{";
                json += "\"ts\":" + String(millis()) + ",";
                json += "\"state\":{";
                json += "\"tank\":{\"temp\":" + String(g_systemState.tank.temp) + ",\"level\":" + String(g_systemState.tank.level) + "},";
                json += "\"grow\":{\"temp\":" + String(g_systemState.grow.temp) + ",\"hum\":" + String(g_systemState.grow.hum) + "}";
                json += "},";
                json += "\"alarms\":{";
                for (int i = 0; i < g_alarmState.count; ++i) {
                    json += "\"" + String(g_alarmState.active_alarms[i].code) + "\":\"" + String(g_alarmState.active_alarms[i].msg) + "\"";
                    if (i < g_alarmState.count - 1) json += ",";
                }
                json += "},";
                json += "\"fw\":\"" + String(g_settings.fw_version) + "\"";
                json += "}";

                // 2. Pack into binary frame [STX, len(2), type(1), data(N), crc(2), ETX]
                uint16_t body_len = json.length() + 1; // +1 for type byte
                uint16_t pkt_len = 2 + body_len + 2 + 1;
                uint8_t* pkt = new uint8_t[pkt_len];

                pkt[0] = 0x02; // STX
                pkt[1] = body_len & 0xFF;
                pkt[2] = (body_len >> 8) & 0xFF;
                pkt[3] = 0x01; // type = status snapshot
                memcpy(&pkt[4], json.c_str(), json.length());
                
                uint16_t crc = crc16_hqx(&pkt[3], body_len);
                pkt[4 + json.length()] = crc & 0xFF;
                pkt[4 + json.length() + 1] = (crc >> 8) & 0xFF;
                pkt[4 + json.length() + 2] = 0x03; // ETX

                // 3. Write to Serial
                Serial.write(pkt, pkt_len);
                delete[] pkt;
            }
            xSemaphoreGive(g_stateMutex);
        }

        // --- 명령 수신 (TODO) ---
        // if (Serial.available()) { ... }

        vTaskDelay(pdMS_TO_TICKS(UART_PERIOD_MS));
    }
}

// ==================== Scheduler Task ====================
// 시간 기반 작업(급여, 조명 등) 스케줄링
void schedulerTask(void *pvParameters) {
    Serial.println("Scheduler Task started.");
    static uint8_t last_minute = 99;

    for (;;) {
        // 1초마다 실행
        vTaskDelay(pdMS_TO_TICKS(SCHEDULER_PERIOD_MS));

        // TODO: RTC로부터 실제 시간 가져오기
        // 임시로 millis()를 사용하여 시간 시뮬레이션 (부팅 후 경과 시간)
        unsigned long now_secs = millis() / 1000;
        uint8_t current_hour = (now_secs / 3600) % 24;
        uint8_t current_minute = (now_secs / 60) % 60;

        // 1분에 한 번만 체크하도록 함
        if (current_minute == last_minute) {
            continue;
        }
        last_minute = current_minute;

        Serial.printf("Scheduler check at %02d:%02d\n", current_hour, current_minute);

        if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            // --- 급여 스케줄 확인 ---
            for (int i = 0; i < 3; ++i) {
                if (g_settings.feed_schedule[i].enabled &&
                    g_settings.feed_schedule[i].hour == current_hour &&
                    g_settings.feed_schedule[i].minute == current_minute) {
                    
                    Serial.printf("Scheduled feeding: %d grams\n", g_settings.feed_schedule[i].grams);
                    // TODO: canHandler.feedNow(g_settings.feed_schedule[i].grams);
                }
            }

            // --- 재배기 LED 스케줄 확인 ---
            if (g_settings.grow_led_schedule.enabled) {
                if (g_settings.grow_led_schedule.on_hour == current_hour &&
                    g_settings.grow_led_schedule.on_minute == current_minute) {
                    
                    Serial.printf("Scheduled LED ON. Brightness: %d\n", g_settings.grow_led_brightness);
                    // TODO: canHandler.setGrowBoxLED(g_settings.grow_led_brightness);
                }
                
                if (g_settings.grow_led_schedule.off_hour == current_hour &&
                    g_settings.grow_led_schedule.off_minute == current_minute) {

                    Serial.println("Scheduled LED OFF.");
                    // TODO: canHandler.setGrowBoxLED(0);
                }
            }
            
            xSemaphoreGive(g_stateMutex);
        }
    }
}
