#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "edge_device_config.h"

struct ScaleReading {
    bool present = false;
    bool stable = false;
    float kg = 0;
    uint32_t at = 0;
};

inline bool scaleAddressMatches(const uint8_t* address, const char* configured) {
    char actual[18], expected[18];
    if (!address || !sweep_config_detail::normalizeMac(configured, expected)) return false;
    std::snprintf(actual, sizeof(actual), "%02X:%02X:%02X:%02X:%02X:%02X",
        address[5], address[4], address[3], address[2], address[1], address[0]);
    return !std::strcmp(actual, expected);
}

// BLE AD: length, type 0x16, UUID 0x181D (LE), flags, weight (LE).
inline bool decodeScaleAdvertisement(const uint8_t* data, size_t length, ScaleReading& out) {
    if (!data) return false;
    for (size_t i = 0; i < length;) {
        const size_t n = data[i];
        if (!n || n >= length - i) return false;
        if (n >= 6 && data[i + 1] == 0x16 && data[i + 2] == 0x1d && data[i + 3] == 0x18) {
            const uint8_t flags = data[i + 4];
            const uint16_t raw = data[i + 5] | (uint16_t(data[i + 6]) << 8);
            const float kg = (flags & 1) ? raw / 100.0f * 0.45359237f : raw / 200.0f;
            if (!std::isfinite(kg) || kg > 400) return false;
            out.present = !(flags & 0x80) && raw != 0;
            out.stable = out.present && (flags & 0x20);
            out.kg = out.present ? kg : 0;
            return true;
        }
        i += n + 1;
    }
    return false;
}
