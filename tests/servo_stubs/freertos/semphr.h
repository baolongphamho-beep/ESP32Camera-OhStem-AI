#pragma once
#include "freertos/FreeRTOS.h"

typedef void* SemaphoreHandle_t;

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    static int dummyMutex = 1;
    return &dummyMutex;
}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) {
    return pdTRUE;
}

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t) {
    return pdTRUE;
}
