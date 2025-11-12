#ifndef DASHBOARD_SCREEN_H
#define DASHBOARD_SCREEN_H

#include <TFT_eSPI.h>
#include "Config.h"
#include <time.h>

extern volatile unsigned long g_epoch_time_s;

class DashboardScreen {
private:
    TFT_eSPI* tft;
    unsigned long lastUpdate;
    bool needsFullRedraw;

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

    void setStateReferences(SystemState* state, LEDState* leds, AlarmState* alarms) {
        systemState = state;
        ledState = leds;
        alarmState = alarms;
    }

    void begin() {
        needsFullRedraw = true;
    }

    void update() {
        if (millis() - lastUpdate < UI_PERIOD_MS) {
            return;
        }
        lastUpdate = millis();

        SystemState localState;
        LEDState localLeds;
        AlarmState localAlarms;

        if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (systemState) memcpy(&localState, systemState, sizeof(SystemState));
            if (ledState) memcpy(&localLeds, ledState, sizeof(LEDState));
            if (alarmState) memcpy(&localAlarms, alarmState, sizeof(AlarmState));
            xSemaphoreGive(g_stateMutex);
        } else {
            Serial.println("Dashboard: Failed to get state mutex.");
            return;
        }

        if (needsFullRedraw) {
            draw(localState, localLeds, localAlarms);
            needsFullRedraw = false;
        } else {
            updateModulePanels(localState);
            updateStatusBar(localAlarms);
            drawLEDStatus(localLeds);
        }
    }

private:
    void draw(const SystemState& s, const LEDState& l, const AlarmState& a) {
        tft->fillScreen(COLOR_BACKGROUND);
        drawHeader(l);
        drawModulePanels(s);
        drawStatusBar(a);
    }

    void drawHeader(const LEDState& l) {
        tft->fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_HEADER_BG);
        tft->setTextColor(COLOR_TEXT, COLOR_HEADER_BG);
        tft->setTextSize(2);
        tft->setTextDatum(ML_DATUM);
        tft->drawString("Smart Farm", 10, HEADER_HEIGHT / 2);
        drawTime();
        drawLEDStatus(l);
    }

    void drawLEDStatus(const LEDState& l) {
        int x = 180;
        int y = HEADER_HEIGHT / 2;
        int ledSize = 6;
        int spacing = 20;
        tft->fillCircle(x, y, ledSize, l.blue ? COLOR_LED_BLUE : COLOR_INACTIVE);
        tft->fillCircle(x + spacing, y, ledSize, l.green ? COLOR_LED_GREEN : COLOR_INACTIVE);
        tft->fillCircle(x + spacing * 2, y, ledSize, l.red ? COLOR_LED_RED : COLOR_INACTIVE);
    }

    void drawTime() {
        time_t now = g_epoch_time_s;
        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);
        char timeStr[6];
        sprintf(timeStr, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        tft->setTextColor(COLOR_TEXT, COLOR_HEADER_BG);
        tft->setTextSize(1);
        tft->setTextDatum(MR_DATUM);
        tft->drawString(timeStr, SCREEN_WIDTH - 10, HEADER_HEIGHT / 2);
    }

    void drawModulePanels(const SystemState& s) {
        int panelWidth = 150;
        int panelHeight = 85;
        int margin = 5;
        int startY = HEADER_HEIGHT + 5;
        drawTankPanel(margin, startY, panelWidth, panelHeight, s);
        drawGrowBoxPanel(margin * 2 + panelWidth, startY, panelWidth, panelHeight, s);
        drawNutrientPanel(margin, startY + panelHeight + margin, panelWidth, panelHeight, s);
        drawFeederPanel(margin * 2 + panelWidth, startY + panelHeight + margin, panelWidth, panelHeight, s);
    }

    void drawTankPanel(int x, int y, int w, int h, const SystemState& s) {
        tft->fillRect(x, y, w, h, COLOR_PANEL_BG);
        tft->drawRect(x, y, w, h, COLOR_TEXT);
        tft->setTextColor(COLOR_TEXT, COLOR_PANEL_BG);
        tft->setTextSize(1);
        tft->setCursor(x + 5, y + 3);
        tft->print("TANK");
        if (s.comm.tank.ok) {
            tft->setCursor(x + 5, y + 18);
            tft->printf("Temp: %.1fC", s.tank.temp);
            tft->setCursor(x + 5, y + 30);
            tft->printf("pH: %.1f", s.tank.ph);
            tft->setCursor(x + 5, y + 54);
            tft->printf("Level: %.0f%%", s.tank.level);
        } else {
            tft->setTextColor(COLOR_ERROR, COLOR_PANEL_BG);
            tft->setTextDatum(MC_DATUM);
            tft->drawString("No Data", x + w / 2, y + h / 2);
        }
    }

    void drawGrowBoxPanel(int x, int y, int w, int h, const SystemState& s) {
        tft->fillRect(x, y, w, h, COLOR_PANEL_BG);
        tft->drawRect(x, y, w, h, COLOR_TEXT);
        tft->setTextColor(COLOR_TEXT, COLOR_PANEL_BG);
        tft->setTextSize(1);
        tft->setCursor(x + 5, y + 3);
        tft->print("GROWBOX");
        if (s.comm.grow.ok) {
            tft->setCursor(x + 5, y + 18);
            tft->printf("Temp: %.1fC", s.grow.temp);
            tft->setCursor(x + 5, y + 30);
            tft->printf("Humid: %.0f%%", s.grow.hum);
            tft->setCursor(x + 5, y + 42);
            tft->printf("LED: %d%%", s.grow.led);
            if (s.grow.leak_bits > 0) {
                tft->setTextColor(COLOR_ERROR, COLOR_PANEL_BG);
                tft->drawString("LEAK!", x + 5, y + 54);
            }
        } else {
            tft->setTextColor(COLOR_ERROR, COLOR_PANEL_BG);
            tft->setTextDatum(MC_DATUM);
            tft->drawString("No Data", x + w / 2, y + h / 2);
        }
    }

    void drawNutrientPanel(int x, int y, int w, int h, const SystemState& s) {
        tft->fillRect(x, y, w, h, COLOR_PANEL_BG);
        tft->drawRect(x, y, w, h, COLOR_TEXT);
        tft->setTextColor(COLOR_TEXT, COLOR_PANEL_BG);
        tft->setTextSize(1);
        tft->setCursor(x + 5, y + 3);
        tft->print("NUTRIENT");
        if (s.comm.nutri.ok) {
            tft->setCursor(x + 5, y + 18);
            tft->printf("A:%d B:%d", s.nutri.remain_ml.A, s.nutri.remain_ml.B);
            tft->setCursor(x + 5, y + 30);
            tft->printf("C:%d D:%d", s.nutri.remain_ml.C, s.nutri.remain_ml.D);
        } else {
            tft->setTextColor(COLOR_ERROR, COLOR_PANEL_BG);
            tft->setTextDatum(MC_DATUM);
            tft->drawString("No Data", x + w / 2, y + h / 2);
        }
    }

    void drawFeederPanel(int x, int y, int w, int h, const SystemState& s) {
        tft->fillRect(x, y, w, h, COLOR_PANEL_BG);
        tft->drawRect(x, y, w, h, COLOR_TEXT);
        tft->setTextColor(COLOR_TEXT, COLOR_PANEL_BG);
        tft->setTextSize(1);
        tft->setCursor(x + 5, y + 3);
        tft->print("FEEDER");
        if (s.comm.feed.ok) {
            tft->setCursor(x + 5, y + 18);
            tft->printf("Food: %d g", s.feed.remain_g);
            if (s.feed.remain_g < 200) {
                tft->setTextColor(COLOR_WARNING, COLOR_PANEL_BG);
                tft->drawString("LOW FOOD!", x + 5, y + 42);
            }
        } else {
            tft->setTextColor(COLOR_ERROR, COLOR_PANEL_BG);
            tft->setTextDatum(MC_DATUM);
            tft->drawString("No Data", x + w / 2, y + h / 2);
        }
    }

    void drawStatusBar(const AlarmState& a) {
        int y = SCREEN_HEIGHT - STATUS_BAR_HEIGHT;
        tft->fillRect(0, y, SCREEN_WIDTH, STATUS_BAR_HEIGHT, COLOR_PANEL_BG);
        tft->setTextSize(1);
        tft->setTextDatum(ML_DATUM);
        if (a.count > 0) {
            tft->setTextColor(COLOR_ERROR, COLOR_PANEL_BG);
            tft->drawString(a.active_alarms[0].msg, 5, y + STATUS_BAR_HEIGHT / 2);
        } else {
            tft->setTextColor(COLOR_OK, COLOR_PANEL_BG);
            tft->drawString("ALL OK", 5, y + STATUS_BAR_HEIGHT / 2);
        }
    }

    void updateModulePanels(const SystemState& s) {
        drawModulePanels(s);
    }

    void updateStatusBar(const AlarmState& a) {
        drawStatusBar(a);
    }
};

#endif // DASHBOARD_SCREEN_H