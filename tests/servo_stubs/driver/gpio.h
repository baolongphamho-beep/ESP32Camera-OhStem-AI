#pragma once
#include "esp_err.h"
#include "servo_fake_state.h"

typedef int gpio_num_t;

inline esp_err_t gpio_reset_pin(gpio_num_t gpio_num) {
    fakeLedcState().gpioResets.push_back(static_cast<int>(gpio_num));
    return ESP_OK;
}
