#ifndef DASHBOARD_SCREEN_H
#define DASHBOARD_SCREEN_H

#include <TFT_eSPI.h>
#include "Config.h"

class DashboardScreen {
private:
    TFT_eSPI* tft;
    unsigned long lastUpdate;
    bool needsFullRedraw;

    // A single pointer to the global system state
    SystemState* systemState;
    LEDState* ledState;
    AlarmState* alarmState;

public:
    DashboardScreen(TFT_eSPI* display) :
        tft(display),
        lastUpdate(0),
        needsFullRedraw(true),
        systemState(nullptr),
        ledState(nullptr),
        alarmState(nullptr) {}

    // Set the reference to the global state objects
    void setStateReferences(SystemState* state, LEDState* leds, AlarmState* alarms) {
        systemState = state;
        ledState = leds;
        alarmState = alarms;
    }

    void begin() {
        needsFullRedraw = true;
        // The initial draw will be handled by the first call to update()
    }

    void update() {
        if (millis() - lastUpdate < UI_PERIOD_MS) {
            return;
        }
        lastUpdate = millis();

        // Safely read the global state and draw the screen
        if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (needsFullRedraw) {
                draw();
            } else {
                updateModulePanels();
                updateStatusBar();
                drawLEDStatus(); // Also update LEDs
            }
            xSemaphoreGive(g_stateMutex);
        } else {
            // Failed to get mutex, skip this update cycle
            Serial.println("Dashboard: Failed to get state mutex.");
        }
    }

    void forceRedraw() {
        needsFullRedraw = true;
    }

private:
    void draw() {
        tft->fillScreen(COLOR_BACKGROUND);
        drawHeader();
        drawModulePanels();
        drawStatusBar();
        needsFullRedraw = false;
    }

    void drawHeader() {
        tft->fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_HEADER_BG);
        tft->setTextColor(COLOR_TEXT);
        tft->setTextSize(2);
        tft->setTextDatum(ML_DATUM);
        tft->drawString("Smart Farm", 10, HEADER_HEIGHT / 2);
        drawTime();
    }

    void drawLEDStatus() {
        if (!ledState) return;

        int x = 180;
        int y = HEADER_HEIGHT / 2;
        int ledSize = 6;
        int spacing = 20;

        tft->fillCircle(x, y, ledSize, ledState->blue ? COLOR_LED_BLUE : COLOR_INACTIVE);
        tft->fillCircle(x + spacing, y, ledSize, ledState->green ? COLOR_LED_GREEN : COLOR_INACTIVE);
        tft->fillCircle(x + spacing * 2, y, ledSize, ledState->red ? COLOR_LED_RED : COLOR_INACTIVE);
    }

    void drawTime() {
        tft->setTextSize(1);
        tft->setTextDatum(MR_DATUM);
        // TODO: 실제 RTC 시간 연동
        tft->drawString("12:34", SCREEN_WIDTH - 10, HEADER_HEIGHT / 2);
    }

    void drawModulePanels() {
        int panelWidth = 150;
        int panelHeight = 85;
        int margin = 5;
        int startY = HEADER_HEIGHT + 5;

        drawTankPanel(margin, startY, panelWidth, panelHeight);
        drawGrowBoxPanel(margin * 2 + panelWidth, startY, panelWidth, panelHeight);
        drawNutrientPanel(margin, startY + panelHeight + margin, panelWidth, panelHeight);
        drawFeederPanel(margin * 2 + panelWidth, startY + panelHeight + margin, panelWidth, panelHeight);
    }

    void drawTankPanel(int x, int y, int w, int h) {
        tft->fillRect(x, y, w, h, COLOR_PANEL_BG);
        tft->drawRect(x, y, w, h, COLOR_TEXT);
        tft->setTextColor(COLOR_TEXT);
        tft->setTextSize(1);
        tft->setCursor(x + 5, y + 3);
        tft->print("TANK");

        if (systemState && systemState->comm.tank.ok) {
            tft->setCursor(x + 5, y + 18);
            tft->printf("Temp: %.1fC", systemState->tank.temp);
            tft->setCursor(x + 5, y + 30);
            tft->printf("pH: %.1f", systemState->tank.ph);
            tft->setCursor(x + 5, y + 54);
            tft->printf("Level: %.0f%%", systemState->tank.level);
        } else {
            tft->setTextColor(COLOR_ERROR);
            tft->setTextDatum(MC_DATUM);
            tft->drawString("No Data", x + w / 2, y + h / 2);
        }
    }

    void drawGrowBoxPanel(int x, int y, int w, int h) {
        tft->fillRect(x, y, w, h, COLOR_PANEL_BG);
        tft->drawRect(x, y, w, h, COLOR_TEXT);
        tft->setTextColor(COLOR_TEXT);
        tft->setTextSize(1);
        tft->setCursor(x + 5, y + 3);
        tft->print("GROWBOX");

        if (systemState && systemState->comm.grow.ok) {
            tft->setCursor(x + 5, y + 18);
            tft->printf("Temp: %.1fC", systemState->grow.temp);
            tft->setCursor(x + 5, y + 30);
            tft->printf("Humid: %.0f%%", systemState->grow.hum);
            tft->setCursor(x + 5, y + 42);
            tft->printf("LED: %d%%", systemState->grow.led);
            if (systemState->grow.leak_bits > 0) {
                tft->setTextColor(COLOR_ERROR);
                tft->drawString("LEAK!", x + 5, y + 54);
            }
        } else {
            tft->setTextColor(COLOR_ERROR);
            tft->setTextDatum(MC_DATUM);
            tft->drawString("No Data", x + w / 2, y + h / 2);
        }
    }

    void drawNutrientPanel(int x, int y, int w, int h) {
        tft->fillRect(x, y, w, h, COLOR_PANEL_BG);
        tft->drawRect(x, y, w, h, COLOR_TEXT);
        tft->setTextColor(COLOR_TEXT);
        tft->setTextSize(1);
        tft->setCursor(x + 5, y + 3);
        tft->print("NUTRIENT");

        if (systemState && systemState->comm.nutri.ok) {
            tft->setCursor(x + 5, y + 18);
            tft->printf("A:%d B:%d", systemState->nutri.remain_ml.A, systemState->nutri.remain_ml.B);
            tft->setCursor(x + 5, y + 30);
            tft->printf("C:%d D:%d", systemState->nutri.remain_ml.C, systemState->nutri.remain_ml.D);
        } else {
            tft->setTextColor(COLOR_ERROR);
            tft->setTextDatum(MC_DATUM);
            tft->drawString("No Data", x + w / 2, y + h / 2);
        }
    }

    void drawFeederPanel(int x, int y, int w, int h) {
        tft->fillRect(x, y, w, h, COLOR_PANEL_BG);
        tft->drawRect(x, y, w, h, COLOR_TEXT);
        tft->setTextColor(COLOR_TEXT);
        tft->setTextSize(1);
        tft->setCursor(x + 5, y + 3);
        tft->print("FEEDER");

        if (systemState && systemState->comm.feed.ok) {
            tft->setCursor(x + 5, y + 18);
            tft->printf("Food: %d g", systemState->feed.remain_g);
            if (systemState->feed.remain_g < 200) {
                tft->setTextColor(COLOR_WARNING);
                tft->drawString("LOW FOOD!", x + 5, y + 42);
            }
        } else {
            tft->setTextColor(COLOR_ERROR);
            tft->setTextDatum(MC_DATUM);
            tft->drawString("No Data", x + w / 2, y + h / 2);
        }
    }

    void drawStatusBar() {
        int y = SCREEN_HEIGHT - STATUS_BAR_HEIGHT;
        tft->fillRect(0, y, SCREEN_WIDTH, STATUS_BAR_HEIGHT, COLOR_PANEL_BG);
        tft->setTextSize(1);
        tft->setTextDatum(ML_DATUM);

        if (alarmState) {
            if (alarmState->count > 0) {
                tft->setTextColor(COLOR_ERROR);
                // Display first active alarm message
                tft->drawString(alarmState->active_alarms[0].msg, 5, y + STATUS_BAR_HEIGHT / 2);
            } else {
                tft->setTextColor(COLOR_OK);
                tft->drawString("ALL OK", 5, y + STATUS_BAR_HEIGHT / 2);
            }
        }
    }

    void updateModulePanels() {
        drawModulePanels();
    }

    void updateStatusBar() {
        drawStatusBar();
    }
};

#endif // DASHBOARD_SCREEN_H
