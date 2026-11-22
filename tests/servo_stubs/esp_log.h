#pragma once

template <typename... Args>
inline void servo_stub_log(const char*, const char*, const Args&...) {}

#define ESP_LOGI(...) servo_stub_log(__VA_ARGS__)
#define ESP_LOGW(...) servo_stub_log(__VA_ARGS__)
#define ESP_LOGE(...) servo_stub_log(__VA_ARGS__)
