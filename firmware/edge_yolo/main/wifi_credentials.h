#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

struct WifiCredentials {
    std::array<uint8_t, 32> ssid{};
    std::array<uint8_t, 64> password{};
    bool open = false;
};

// ESP-IDF fields are byte arrays: at maximum length no trailing NUL fits.
// Return only fixed error messages, never credentials.
inline const char* prepareWifiCredentials(std::string_view ssid,
                                           std::string_view password,
                                           WifiCredentials& out) {
    if (ssid.size() > 32 || ssid.find('\0') != std::string_view::npos)
        return "SSID must be at most 32 bytes without NUL";
    if (password.find('\0') != std::string_view::npos)
        return "Wi-Fi password cannot contain NUL";
    if (!password.empty() && (password.size() < 8 || password.size() > 64))
        return "Wi-Fi password must be empty, 8-63 bytes, or 64 hex digits";
    if (password.size() == 64) {
        for (char ch : password) {
            if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
                  (ch >= 'A' && ch <= 'F')))
                return "A 64-byte Wi-Fi key must contain only hex digits";
        }
    }
    WifiCredentials value;
    if (!ssid.empty()) std::memcpy(value.ssid.data(), ssid.data(), ssid.size());
    if (!password.empty()) std::memcpy(value.password.data(), password.data(), password.size());
    value.open = password.empty();
    out = value;
    return nullptr;
}
