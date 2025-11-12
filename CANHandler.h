#ifndef CAN_HANDLER_H
#define CAN_HANDLER_H

#include <Arduino.h>
#include "Config.h"
#include "driver/twai.h" // ESP32 CAN driver (TWAI)

class CANHandler {
public:
    CANHandler() : initialized(false) {}

    bool begin() {
        twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX, (gpio_num_t)CAN_RX, TWAI_MODE_NORMAL);
        twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
        twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

        if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
            Serial.println("CAN driver install failed");
            return false;
        }
        if (twai_start() != ESP_OK) {
            Serial.println("CAN start failed");
            return false;
        }
        initialized = true;
        Serial.println("CAN initialized successfully");
        return true;
    }

    void stop() {
        if (initialized) {
            twai_stop();
            twai_driver_uninstall();
            initialized = false;
            Serial.println("CAN stopped");
        }
    }

    bool receiveMessage(twai_message_t &message) {
        if (!initialized) return false;
        return twai_receive(&message, pdMS_TO_TICKS(10)) == ESP_OK;
    }

    bool sendMessage(uint32_t id, const uint8_t* data, uint8_t length) {
        if (!initialized) return false;
        twai_message_t message;
        message.identifier = id;
        message.data_length_code = length;
        memcpy(message.data, data, length);
        return twai_transmit(&message, pdMS_TO_TICKS(100)) == ESP_OK;
    }

    void parseFrame(const twai_message_t &message, SystemState &state) {
        updateCommStatus(message.identifier, state);

        switch (message.identifier) {
            case TANK:
                parseTankFrame(message, state.tank);
                break;
            case GROW:
                parseGrowFrame(message, state.grow);
                break;
            case NUTRI:
                parseNutriFrame(message, state.nutri);
                break;
            case FEED:
                parseFeedFrame(message, state.feed);
                break;
        }
    }

    bool isInitialized() const {
        return initialized;
    }

    // --- Control Command Senders ---

    void setGrowBoxLED(uint8_t brightness) {
        uint8_t data[3] = {CMD, 0x20, brightness}; // 0x20: LED Brightness CMD
        sendMessage(GROW, data, sizeof(data));
    }

    void setGrowBoxSchedule(bool enable) {
        uint8_t data[3] = {CMD, 0x21, (uint8_t)enable}; // 0x21: Schedule Enable CMD
        sendMessage(GROW, data, sizeof(data));
    }

    void setNutrientPump(bool on) {
        uint8_t data[3] = {CMD, 0x30, (uint8_t)on}; // 0x30: Pump On/Off
        sendMessage(NUTRI, data, sizeof(data));
    }

    void feedNow(uint8_t grams) {
        uint8_t data[3] = {CMD, 0x40, grams}; // 0x40: Feed Now
        sendMessage(FEED, data, sizeof(data));
    }

private:
    bool initialized;

    void updateCommStatus(uint32_t id, SystemState &state) {
        unsigned long now = millis();
        switch (id) {
            case TANK:
                state.comm.tank.last_ts_ms = now;
                state.comm.tank.ok = true;
                break;
            case GROW:
                state.comm.grow.last_ts_ms = now;
                state.comm.grow.ok = true;
                break;
            case NUTRI:
                state.comm.nutri.last_ts_ms = now;
                state.comm.nutri.ok = true;
                break;
            case FEED:
                state.comm.feed.last_ts_ms = now;
                state.comm.feed.ok = true;
                break;
        }
    }

    template<typename T>
    void unpack(T& var, const uint8_t* data, int offset) {
        memcpy(&var, &data[offset], sizeof(T));
    }

    void parseTankFrame(const twai_message_t &message, TankState &state) {
        // Python: struct.unpack("<ffffff", payload) -> 6 floats = 24 bytes
        // This would require multiple CAN frames. Let's assume a simplified protocol
        // where one frame (8 bytes) carries the most critical data.
        // Protocol: [CMD, temp(float), ph(float)] -> Not possible in 8 bytes.
        // Let's define a more realistic protocol for one frame:
        // [CMD_SENS, temp(2 bytes, 0.1 scale), level(1 byte, %), ph(1 byte, 0.1 scale), tds(2 bytes, ppm)]
        if (message.data[0] != SENS || message.data_length_code < 7) return;
        
        int16_t temp_raw;
        unpack(temp_raw, message.data, 1);
        state.temp = temp_raw / 10.0f;

        unpack(state.level, message.data, 3); // Assuming level is just one byte for %

        uint8_t ph_raw;
        unpack(ph_raw, message.data, 4);
        state.ph = ph_raw / 10.0f;

        uint16_t tds_raw;
        unpack(tds_raw, message.data, 5);
        state.tds = tds_raw;
    }

    void parseGrowFrame(const twai_message_t &message, GrowState &state) {
        // Python: struct.unpack("<ffBB", payload) -> float, float, byte, byte = 10 bytes
        // Simplified protocol: [CMD_SENS, temp(2 bytes, 0.1 scale), hum(1 byte, %), leak(1 byte, bits), led(1 byte, %)]
        if (message.data[0] != SENS || message.data_length_code < 6) return;
        
        int16_t temp_raw;
        unpack(temp_raw, message.data, 1);
        state.temp = temp_raw / 10.0f;

        uint8_t hum_raw;
        unpack(hum_raw, message.data, 3);
        state.hum = hum_raw;

        unpack(state.leak_bits, message.data, 4);
        unpack(state.led, message.data, 5);
    }

    void parseNutriFrame(const twai_message_t &message, NutriState &state) {
        // Python: struct.unpack("<BBBBHHHH", payload) -> 4 bytes + 4*2 bytes = 12 bytes
        // Simplified: [CMD_SENS, ratioA, ratioB, ratioC, ratioD, remainA(2 bytes), remainB(2 bytes)] -> 9 bytes, too big
        // Let's just parse remaining amounts for two channels
        // Protocol: [CMD_SENS, remainA(2 bytes), remainB(2 bytes), remainC(2 bytes), remainD(2 bytes)]
        if (message.data[0] != SENS || message.data_length_code < 9) return;
        unpack(state.remain_ml.A, message.data, 1);
        unpack(state.remain_ml.B, message.data, 3);
        unpack(state.remain_ml.C, message.data, 5);
        unpack(state.remain_ml.D, message.data, 7);
    }

    void parseFeedFrame(const twai_message_t &message, FeedState &state) {
        // Python: struct.unpack("<H", payload) -> 2 bytes
        if (message.data[0] != SENS || message.data_length_code < 3) return;
        unpack(state.remain_g, message.data, 1);
    }
};

#endif // CAN_HANDLER_H