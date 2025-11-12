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
SystemState g_systemState;
LEDState g_ledState;
AlarmState g_alarmState;
SystemSettings g_settings;
SemaphoreHandle_t g_stateMutex;
volatile unsigned long g_epoch_time_s = 0; // Synced from server

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

// UI Input State
volatile int encoderValue = 0;
volatile unsigned long lastEncoderTime = 0;

// 화면 객체
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
    memset(&g_systemState, 0, sizeof(SystemState));
    memset(&g_ledState, 0, sizeof(LEDState));
    memset(&g_alarmState, 0, sizeof(AlarmState));
    
    g_systemState.serverConnected = false;
    g_epoch_time_s = 1672531200; // Default to 2023-01-01 00:00:00 UTC

    memset(&g_settings, 0, sizeof(SystemSettings));
    strcpy(g_settings.fw_version, "0.1.0");
    g_settings.time_sync_from_server = true;
    for(int i=0; i<4; ++i) g_settings.module_enable[i] = true;

    g_settings.feed_schedule[0] = {7, 30, 5, true};
    g_settings.feed_schedule[1] = {12, 0, 5, true};
    g_settings.feed_schedule[2] = {18, 30, 5, true};

    g_settings.grow_led_schedule = {8, 0, 22, 0, true};
    g_settings.grow_led_brightness = 80;

    loadSettings(g_settings);
}

// ==================== Setup ====================
void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== Smart Farm Display Controller (FreeRTOS) ===");

    init_nvs();
    g_stateMutex = xSemaphoreCreateMutex();
    if (g_stateMutex == NULL) {
        Serial.println("Error: Failed to create state mutex!");
        while(1);
    }

    initSystemData();

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(COLOR_BACKGROUND);

    ledController.begin();
    ledController.startupSequence();

    if (!canHandler.begin()) {
      Serial.println("CAN init failed - running without CAN");
    }

    dashboard = new DashboardScreen(&tft);
    dashboard->setStateReferences(&g_systemState, &g_ledState, &g_alarmState);

    tankScreen = new TankScreen(&tft);
    tankScreen->setStateReference(&g_systemState);
    tankScreen->setCANHandler(&canHandler);

    growBoxScreen = new GrowBoxScreen(&tft);
    growBoxScreen->setStateReference(&g_systemState);
    growBoxScreen->setCANHandler(&canHandler);

    nutrientScreen = new NutrientScreen(&tft);
    nutrientScreen->setStateReference(&g_systemState);
    nutrientScreen->setCANHandler(&canHandler);

    feederScreen = new FeederScreen(&tft);
    feederScreen->setStateReference(&g_systemState);
    feederScreen->setCANHandler(&canHandler);

    logScreen = new LogScreen(&tft);
    logScreen->setStateReference(&g_alarmState);

    settingsScreen = new SettingsScreen(&tft);
    settingsScreen->setSettingsReference(&g_settings);

    Serial.println("Hardware and screens initialized.");

    xTaskCreatePinnedToCore(uiTask, "UITask", TASK_UI_STACK_SIZE, NULL, TASK_UI_PRIORITY, &g_uiTaskHandle, TASK_UI_CORE);
    xTaskCreatePinnedToCore(canTask, "CANTask", TASK_CAN_STACK_SIZE, NULL, TASK_CAN_PRIORITY, &g_canTaskHandle, TASK_CAN_CORE);
    xTaskCreatePinnedToCore(canWatchdogTask, "CANWatchdogTask", 2048, NULL, TASK_CAN_PRIORITY, &g_canWatchdogTaskHandle, TASK_CAN_CORE);
    xTaskCreatePinnedToCore(uartTask, "UARTTask", TASK_UART_STACK_SIZE, NULL, TASK_UART_PRIORITY, &g_uartTaskHandle, TASK_UART_CORE);
    xTaskCreatePinnedToCore(schedulerTask, "SchedulerTask", TASK_SCHEDULER_STACK_SIZE, NULL, TASK_SCHEDULER_PRIORITY, &g_schedulerTaskHandle, TASK_SCHEDULER_CORE);

    Serial.println("All tasks created. System is running.");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}

// ==================== UI Task ====================
void uiTask(void *pvParameters) {
    Serial.println("UI Task started.");
    int lastEncoderValue = 0;
    bool buttonPressed = false;
    unsigned long lastButtonTime = 0;
    
    pinMode(ENCODER_CLK, INPUT_PULLUP);
    pinMode(ENCODER_DT, INPUT_PULLUP);
    pinMode(ENCODER_SW, INPUT_PULLUP);

    // NOTE: Encoder interrupt is disabled. Re-enable after fixing hardware/config.
    // attachInterrupt(digitalPinToInterrupt(ENCODER_CLK), []() {
    //     if (millis() - lastEncoderTime < 3) return;
    //     lastEncoderTime = millis();
    //     if (digitalRead(ENCODER_DT) != digitalRead(ENCODER_CLK)) {
    //         encoderValue++;
    //     } else {
    //         encoderValue--;
    //     }
    // }, CHANGE);
    
    for (;;) {
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
        
        switch (screenManager.getCurrentScreen()) {
            case SCREEN_DASHBOARD:
                if (direction != 0) {
                    if (direction > 0) screenManager.nextScreen(); else screenManager.prevScreen();
                }
                if (clicked) screenManager.setScreen(SCREEN_TANK);
                break;
            case SCREEN_TANK:
                if (direction != 0) tankScreen->onEncoderRotate(direction);
                if (clicked) tankScreen->onButtonClick();
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
                if (clicked) screenManager.goToDashboard();
                break;
            case SCREEN_SETTINGS:
                if (direction != 0) settingsScreen->onEncoderRotate(direction);
                if (clicked) settingsScreen->onButtonClick();
                if (clicked && settingsScreen->getSelectedItem() == SETTINGS_SAVE_AND_BACK) {
                    screenManager.goToDashboard();
                }
                break;
            default:
                if (direction != 0) {
                    if (direction > 0) screenManager.nextScreen(); else screenManager.prevScreen();
                }
                break;
        }
        
        if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            ledController.setBlue(g_ledState.blue);
            ledController.setGreen(g_ledState.green);
            ledController.setRed(g_ledState.red);
            xSemaphoreGive(g_stateMutex);
        }

        bool needsRedraw = screenManager.getAndClearNeedsRedrawFlag();
        switch (screenManager.getCurrentScreen()) {
            case SCREEN_DASHBOARD: if (needsRedraw) dashboard->begin(); dashboard->update(); break;
            case SCREEN_TANK: if (needsRedraw) tankScreen->begin(); tankScreen->update(); break;
            case SCREEN_GROWBOX: if (needsRedraw) growBoxScreen->begin(); growBoxScreen->update(); break;
            case SCREEN_NUTRIENT: if (needsRedraw) nutrientScreen->begin(); nutrientScreen->update(); break;
            case SCREEN_FEEDER: if (needsRedraw) feederScreen->begin(); feederScreen->update(); break;
            case SCREEN_LOG: if (needsRedraw) logScreen->begin(); logScreen->update(); break;
            case SCREEN_SETTINGS: if (needsRedraw) settingsScreen->begin(); settingsScreen->update(); break;
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
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ==================== CAN Task ====================
void canTask(void *pvParameters) {
    Serial.println("CAN Task started.");
    twai_message_t rx_message;
    for (;;) {
        if (canHandler.receiveMessage(rx_message)) {
            if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                canHandler.parseFrame(rx_message, g_systemState);
                if (rx_message.identifier == GROW) {
                    if (g_systemState.grow.leak_bits > 0) {
                        raise_alarm(g_alarmState, g_ledState, "E-LEAK", "Leak detected!");
                    } else {
                        clear_alarm(g_alarmState, g_ledState, "E-LEAK");
                    }
                }
                xSemaphoreGive(g_stateMutex);
            } else {
                Serial.println("CAN Task: Failed to take mutex.");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(CAN_PERIOD_MS));
    }
}

// ==================== CAN Watchdog Task ====================
void canWatchdogTask(void *pvParameters) {
    Serial.println("CAN Watchdog Task started.");
    for (;;) {
        if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            bool all_modules_ok = true;
            unsigned long now = millis();
            if (g_settings.module_enable[0] && now - g_systemState.comm.tank.last_ts_ms > CAN_WATCHDOG_TIMEOUT_MS) {
                if (g_systemState.comm.tank.ok) g_systemState.comm.tank.ok = false;
                all_modules_ok = false;
            }
            if (g_settings.module_enable[1] && now - g_systemState.comm.grow.last_ts_ms > CAN_WATCHDOG_TIMEOUT_MS) {
                if (g_systemState.comm.grow.ok) g_systemState.comm.grow.ok = false;
                all_modules_ok = false;
            }
            if (g_settings.module_enable[2] && now - g_systemState.comm.nutri.last_ts_ms > CAN_WATCHDOG_TIMEOUT_MS) {
                if (g_systemState.comm.nutri.ok) g_systemState.comm.nutri.ok = false;
                all_modules_ok = false;
            }
            if (g_settings.module_enable[3] && now - g_systemState.comm.feed.last_ts_ms > CAN_WATCHDOG_TIMEOUT_MS) {
                if (g_systemState.comm.feed.ok) g_systemState.comm.feed.ok = false;
                all_modules_ok = false;
            }
            g_ledState.green = all_modules_ok;
            if (!all_modules_ok) {
                raise_alarm(g_alarmState, g_ledState, "E-CAN-LOST", "Module comm lost");
            } else {
                clear_alarm(g_alarmState, g_ledState, "E-CAN-LOST");
            }
            xSemaphoreGive(g_stateMutex);
        } else {
            Serial.println("Watchdog: Failed to take mutex.");
        }
        vTaskDelay(pdMS_TO_TICKS(CAN_WATCHDOG_TIMEOUT_MS));
    }
}

// ==================== UART Task ====================
uint16_t crc16_hqx(const uint8_t *data, uint16_t length) {
    uint16_t crc = 0xFFFF;
    while (length--) {
        crc ^= *data++;
        for (uint8_t i = 0; i < 8; i++) {
            if (crc & 0x0001) crc = (crc >> 1) ^ 0x8408;
            else crc >>= 1;
        }
    }
    return crc;
}

void uartTask(void *pvParameters) {
    Serial.println("UART Task started.");
    static bool serverSimConnected = true;
    enum RxState { RX_IDLE, RX_LEN1, RX_LEN2, RX_PAYLOAD, RX_CRC1, RX_CRC2, RX_ETX };
    RxState rxState = RX_IDLE;
    uint8_t rxBuffer[UART_RX_BUFFER_SIZE];
    uint16_t payloadLen = 0, payloadIdx = 0, receivedCrc = 0;
    char jsonBuffer[512];
    SystemState localState;
    AlarmState localAlarms;
    SystemSettings localSettings;
    unsigned long local_epoch_time_s;

    for (;;) {
        if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            g_systemState.serverConnected = serverSimConnected;
            g_ledState.blue = serverSimConnected;
            memcpy(&localState, &g_systemState, sizeof(SystemState));
            memcpy(&localAlarms, &g_alarmState, sizeof(AlarmState));
            memcpy(&localSettings, &g_settings, sizeof(SystemSettings));
            local_epoch_time_s = g_epoch_time_s;
            xSemaphoreGive(g_stateMutex);
        } else {
            Serial.println("uartTask: Failed to get mutex for data copy.");
            vTaskDelay(pdMS_TO_TICKS(UART_PERIOD_MS));
            continue;
        }

        if (localState.serverConnected) {
            int len = snprintf(jsonBuffer, sizeof(jsonBuffer),
                "{\"ts\":%lu,\"state\":{\"tank\":{\"temp\":%.1f,\"level\":%.1f},\"grow\":{\"temp\":%.1f,\"hum\":%.1f}},\"alarms\":{",
                local_epoch_time_s,
                localState.tank.temp, localState.tank.level,
                localState.grow.temp, localState.grow.hum
            );
            for (int i = 0; i < localAlarms.count; ++i) {
                len += snprintf(jsonBuffer + len, sizeof(jsonBuffer) - len,
                    "\"%s\":\"%s\"%s",
                    localAlarms.active_alarms[i].code,
                    localAlarms.active_alarms[i].msg,
                    (i < localAlarms.count - 1) ? "," : ""
                );
            }
            len += snprintf(jsonBuffer + len, sizeof(jsonBuffer) - len,
                "},\"fw\":\"%s\"}",
                localSettings.fw_version
            );

            uint16_t body_len = len + 1;
            uint16_t pkt_len = 1 + 2 + body_len + 2 + 1;
            uint8_t* pkt = new uint8_t[pkt_len];

            pkt[0] = 0x02;
            pkt[1] = body_len & 0xFF;
            pkt[2] = (body_len >> 8) & 0xFF;
            pkt[3] = 0x01;
            memcpy(&pkt[4], jsonBuffer, len);
            
            uint16_t crc = crc16_hqx(&pkt[3], body_len);
            pkt[3 + body_len] = crc & 0xFF;
            pkt[3 + body_len + 1] = (crc >> 8) & 0xFF;
            pkt[3 + body_len + 2] = 0x03;

            Serial.write(pkt, pkt_len);
            delete[] pkt;
        }

        while (Serial.available() > 0) {
            uint8_t byte = Serial.read();
            switch (rxState) {
                case RX_IDLE: if (byte == 0x02) rxState = RX_LEN1; break;
                case RX_LEN1: payloadLen = byte; rxState = RX_LEN2; break;
                case RX_LEN2:
                    payloadLen |= (byte << 8);
                    if (payloadLen > 0 && payloadLen <= UART_RX_BUFFER_SIZE) {
                        payloadIdx = 0; rxState = RX_PAYLOAD;
                    } else { rxState = RX_IDLE; }
                    break;
                case RX_PAYLOAD:
                    rxBuffer[payloadIdx++] = byte;
                    if (payloadIdx >= payloadLen) rxState = RX_CRC1;
                    break;
                case RX_CRC1: receivedCrc = byte; rxState = RX_CRC2; break;
                case RX_CRC2: receivedCrc |= (byte << 8); rxState = RX_ETX; break;
                case RX_ETX:
                    if (byte == 0x03) {
                        if (receivedCrc == crc16_hqx(rxBuffer, payloadLen)) {
                            if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                                char* jsonPayload = (char*)&rxBuffer[1];
                                int jsonLen = payloadLen - 1;
                                jsonPayload[jsonLen] = '\0';
                                Serial.printf("UART CMD Received: %s\n", jsonPayload);
                                xSemaphoreGive(g_stateMutex);
                            }
                        } else { Serial.println("UART CMD: CRC Error"); }
                    }
                    rxState = RX_IDLE;
                    break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(UART_PERIOD_MS));
    }
}

// ==================== Scheduler Task ====================
void schedulerTask(void *pvParameters) {
    Serial.println("Scheduler Task started.");
    static uint8_t last_minute = 99;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(SCHEDULER_PERIOD_MS));

        g_epoch_time_s++;

        time_t now = g_epoch_time_s;
        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);
        uint8_t current_hour = timeinfo.tm_hour;
        uint8_t current_minute = timeinfo.tm_min;

        if (current_minute == last_minute) continue;
        last_minute = current_minute;

        if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            for (int i = 0; i < 3; ++i) {
                if (g_settings.feed_schedule[i].enabled &&
                    g_settings.feed_schedule[i].hour == current_hour &&
                    g_settings.feed_schedule[i].minute == current_minute) {
                    Serial.printf("Scheduled feeding: %d grams\n", g_settings.feed_schedule[i].grams);
                    canHandler.feedNow(g_settings.feed_schedule[i].grams);
                }
            }
            if (g_settings.grow_led_schedule.enabled) {
                if (g_settings.grow_led_schedule.on_hour == current_hour &&
                    g_settings.grow_led_schedule.on_minute == current_minute) {
                    Serial.printf("Scheduled LED ON. Brightness: %d\n", g_settings.grow_led_brightness);
                    canHandler.setGrowBoxLED(g_settings.grow_led_brightness);
                }
                if (g_settings.grow_led_schedule.off_hour == current_hour &&
                    g_settings.grow_led_schedule.off_minute == current_minute) {
                    Serial.println("Scheduled LED OFF.");
                    canHandler.setGrowBoxLED(0);
                }
            }
            xSemaphoreGive(g_stateMutex);
        }
    }
}
