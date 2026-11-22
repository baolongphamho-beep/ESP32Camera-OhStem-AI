#pragma once
#include "freertos/FreeRTOS.h"
#include "servo_fake_state.h"

inline void vTaskDelay(TickType_t ticks) {
    fakeLedcState().delaysMs.push_back(static_cast<uint32_t>(ticks));
}
