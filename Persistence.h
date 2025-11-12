#ifndef PERSISTENCE_H
#define PERSISTENCE_H

#include <nvs_flash.h>
#include <nvs.h>
#include "Config.h"

#define NVS_NAMESPACE "storage"

void init_nvs() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    Serial.println("NVS Initialized.");
}

bool saveSettings(const SystemSettings& settings) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        Serial.printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(my_handle, "settings", &settings, sizeof(SystemSettings));
    if (err != ESP_OK) {
        Serial.printf("Error (%s) writing settings to NVS!\n", esp_err_to_name(err));
        nvs_close(my_handle);
        return false;
    }

    err = nvs_commit(my_handle);
    if (err != ESP_OK) {
        Serial.printf("Error (%s) committing settings to NVS!\n", esp_err_to_name(err));
        nvs_close(my_handle);
        return false;
    }

    nvs_close(my_handle);
    Serial.println("Settings saved to NVS.");
    return true;
}

bool loadSettings(SystemSettings& settings) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        Serial.printf("Error (%s) opening NVS handle for reading!\n", esp_err_to_name(err));
        return false;
    }

    size_t required_size = sizeof(SystemSettings);
    err = nvs_get_blob(my_handle, "settings", &settings, &required_size);
    
    nvs_close(my_handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        Serial.println("Settings not found in NVS. Using default values.");
        return false;
    } else if (err != ESP_OK) {
        Serial.printf("Error (%s) reading settings from NVS!\n", esp_err_to_name(err));
        return false;
    }

    Serial.println("Settings loaded from NVS.");
    return true;
}

#endif // PERSISTENCE_H
