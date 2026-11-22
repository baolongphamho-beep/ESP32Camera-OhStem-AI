#pragma once

typedef int esp_err_t;
static constexpr esp_err_t ESP_OK = 0;
static constexpr esp_err_t ESP_FAIL = -1;

inline const char* esp_err_to_name(esp_err_t rc) {
    return (rc == ESP_OK) ? "ESP_OK" : "ESP_FAIL";
}
