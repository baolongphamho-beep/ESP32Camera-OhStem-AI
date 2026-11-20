#pragma once
// SSV1 packet (same framing as camera/usb_protocol.py): "SSV1", uint32 json length,
// uint32 jpeg length, uint32 CRC32(json + jpeg), all little-endian, then the JSON.
// Lets the Arduino servo-sweep firmware answer usb_viewer.py (config ack, status).
#include <cstddef>
#include <cstdint>
#include <cstring>

inline uint32_t ssv1Crc32(const uint8_t* data, size_t n, uint32_t crc = 0) {
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

// Writes header + JSON into out; returns total size, or 0 if it does not fit.
inline size_t ssv1Packet(const char* json, uint8_t* out, size_t capacity) {
    const size_t n = json ? std::strlen(json) : 0;
    if (n == 0 || n > 4096 || capacity < 16 + n) return 0;
    const uint32_t fields[3] = {(uint32_t)n, 0u, ssv1Crc32((const uint8_t*)json, n)};
    std::memcpy(out, "SSV1", 4);
    for (int f = 0; f < 3; ++f)
        for (int b = 0; b < 4; ++b) out[4 + 4 * f + b] = (uint8_t)(fields[f] >> (8 * b));
    std::memcpy(out + 16, json, n);
    return 16 + n;
}
