#pragma once
// TV5: reply to the USB line "CONFIG?" with the configuration the firmware is USING
// (loaded from NVS at boot or after APPLY), so `python camera/usb_viewer.py --show-config`
// can check a calibration after a reboot. Same key names as BEGIN/CFG/APPLY.
// The Wi-Fi password is never sent, only whether one is set.
// Template on the config type so tests/config_readback_test.cpp runs on a PC without
// FreeRTOS (EdgeConfig itself lives in edge_io.h).
#include <cstddef>
#include <cstdio>

namespace config_readback {

// JSON-safe copy: quotes, backslashes and non-printable bytes become '?'.
inline void jsonText(const char* in, char* out, size_t size) {
    if (!out || !size) return;
    size_t j = 0;
    for (size_t i = 0; in && in[i] && j + 1 < size; ++i) {
        const unsigned char ch = static_cast<unsigned char>(in[i]);
        out[j++] = (ch == '"' || ch == '\\' || ch < 0x20 || ch > 0x7e) ? '?' : char(ch);
    }
    out[j] = '\0';
}

template <class Config>
bool toJson(const Config& c, char* out, size_t size) {
    char ssid[sizeof(c.ssid)], camera[sizeof(c.cameraUrl)], mac[sizeof(c.scaleMac)];
    jsonText(c.ssid, ssid, sizeof(ssid));
    jsonText(c.cameraUrl, camera, sizeof(camera));
    jsonText(c.scaleMac, mac, sizeof(mac));
    const int n = std::snprintf(out, size,
        "{\"kind\":\"config_values\",\"ssid\":\"%s\",\"password_set\":%s,\"camera\":\"%s\","
        "\"mac\":\"%s\",\"mode\":\"%s\",\"distance\":%.3f,\"reference\":%.1f,\"focal\":%.4f,"
        "\"height\":%.1f,\"tilt\":%.2f,\"hold_sec\":%u,\"servo_pin\":%d,\"servo_min\":%.1f,"
        "\"servo_max\":%.1f,\"servo_bottom\":%.1f,\"servo_top\":%.1f,\"servo_step\":%.2f,"
        "\"servo_level\":%.1f,\"servo_dir\":%d,\"servo_settle\":%u,\"revision\":%lu}",
        ssid, c.password[0] ? "true" : "false", camera, mac, c.groundMode ? "ground" : "fixed",
        double(c.distanceM), double(c.referenceCm), double(c.focalPx), double(c.cameraHeightCm),
        double(c.tiltDeg), unsigned(c.holdSec), int(c.servoPin), double(c.servoMinDeg),
        double(c.servoMaxDeg), double(c.servoBottomDeg), double(c.servoTopDeg),
        double(c.servoStepDeg), double(c.servoLevelDeg), int(c.servoDir), unsigned(c.servoSettleMs),
        static_cast<unsigned long>(c.revision));
    return n > 0 && static_cast<size_t>(n) < size;
}

}  // namespace config_readback
