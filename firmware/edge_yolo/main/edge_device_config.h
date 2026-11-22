#pragma once
#include "../../../lib/EdgeSweep/edge_config.h"
#include "camera_control.h"
#include <cstdio>

// Transport settings extend the exact configuration used by Arduino and PC tests.
struct EdgeDeviceConfig : EdgeConfig {
    char ssid[33] = {};
    char password[65] = {};
    char cameraUrl[160] = "http://10.127.7.68/capture";
    // Pushed to the camera by the capture worker (camera_control.h); -1 = do not touch.
    int camFramesize = -1;  // "cam_framesize": 5 = 320x240 on the LAB camera
    int camQuality = -1;    // "cam_quality": 4..63, lower = sharper/bigger JPEG
    uint32_t revision = 0; // RAM only; invalidates captures after APPLY.
    EdgeDeviceConfig() { std::strcpy(scaleMac, "70:87:9E:EA:3A:E0"); }
};

inline ConfigResult applyDeviceConfigKey(EdgeDeviceConfig& c, const char* key, const char* value) {
    if (!key || !value) return ConfigResult::Invalid;
    char* dst = nullptr;
    size_t capacity = 0;
    if (!std::strcmp(key, "ssid")) { dst = c.ssid; capacity = sizeof(c.ssid); }
    if (!std::strcmp(key, "password")) { dst = c.password; capacity = sizeof(c.password); }
    if (!std::strcmp(key, "camera")) {
        if (std::strncmp(value, "http://", 7) || !value[7]) return ConfigResult::Invalid;
        dst = c.cameraUrl; capacity = sizeof(c.cameraUrl);
    }
    if (dst) {
        if (std::strlen(value) >= capacity || std::strpbrk(value, "\r\n")) return ConfigResult::Invalid;
        std::snprintf(dst, capacity, "%s", value);
        return ConfigResult::Applied;
    }
    if (!std::strcmp(key, "cam_framesize") || !std::strcmp(key, "cam_quality")) {
        float v;
        if (!sweep_config_detail::parseNumber(value, v) || !sweep_config_detail::whole(v))
            return ConfigResult::Invalid;
        const int n = int(v);
        const bool frame = !std::strcmp(key, "cam_framesize");
        if (frame ? !camera_control::framesizeValid(n) : !camera_control::qualityValid(n))
            return ConfigResult::Invalid;
        (frame ? c.camFramesize : c.camQuality) = n;
        return ConfigResult::Applied;
    }
    if (!std::strcmp(key, "focal")) {
        float focal;
        if (!sweep_config_detail::parseNumber(value, focal) || focal <= 0) return ConfigResult::Invalid;
    }
    if (!std::strcmp(key, "scaleMac")) key = "mac";
    return applyConfigKey(c, key, value);
}

inline bool deviceConfigValid(const EdgeDeviceConfig& c) {
    char mac[18];
    return configValid(c) && sweep_config_detail::normalizeMac(c.scaleMac, mac) &&
           c.servoPin >= 0 && c.servoPin <= 48 &&
           inServoLimits(c.bottomDeg) && inServoLimits(c.topDeg) && inServoLimits(c.levelDeg) &&
           std::isfinite(c.stepDeg) && c.stepDeg <= 20 && c.settleMs <= 3000 &&
           c.frameDelayMs <= 3000 && std::isfinite(c.speedDps) && c.speedDps >= 1 && c.speedDps <= 60 &&
           std::isfinite(c.holdSec) && c.holdSec >= 0 && c.holdSec <= 120 &&
           std::isfinite(c.focal) && c.focal >= .01f && c.focal <= 20 &&
           std::isfinite(c.cameraHeightCm) && c.cameraHeightCm <= 400 &&
           !std::strncmp(c.cameraUrl, "http://", 7) && c.cameraUrl[7] &&
           camera_control::framesizeValid(c.camFramesize) && camera_control::qualityValid(c.camQuality);
}

// JSON-safe copy of a configured string (quotes/backslashes/control chars replaced).
inline void configJsonText(const char* in, char* out, size_t size) {
    size_t j = 0;
    for (size_t i = 0; in && in[i] && j + 1 < size; ++i) {
        const unsigned char ch = static_cast<unsigned char>(in[i]);
        out[j++] = (ch == '"' || ch == '\\' || ch < 0x20 || ch > 0x7e) ? '?' : char(ch);
    }
    if (size) out[j] = '\0';
}

// Reply to USB "CONFIG?": the values the firmware is USING (loaded from NVS), with the
// same key names as BEGIN/CFG/APPLY. The Wi-Fi password is never sent, only whether
// one is set. Returns false if `size` is too small.
inline bool deviceConfigJson(const EdgeDeviceConfig& c, char* out, size_t size) {
    char ssid[sizeof(c.ssid)], camera[sizeof(c.cameraUrl)], mac[sizeof(c.scaleMac)];
    configJsonText(c.ssid, ssid, sizeof(ssid));
    configJsonText(c.cameraUrl, camera, sizeof(camera));
    configJsonText(c.scaleMac, mac, sizeof(mac));
    const int n = std::snprintf(out, size,
        "{\"kind\":\"config_values\",\"servo_pin\":%d,\"servo_bottom\":%.2f,\"servo_top\":%.2f,"
        "\"servo_step\":%.2f,\"servo_level\":%.2f,\"servo_dir\":%d,\"servo_settle\":%lu,"
        "\"servo_delay\":%lu,\"servo_dps\":%.2f,\"hold_sec\":%.2f,\"height\":%.2f,\"focal\":%.4f,"
        "\"mac\":\"%s\",\"camera\":\"%s\",\"cam_framesize\":%d,\"cam_quality\":%d,"
        "\"ssid\":\"%s\",\"password_set\":%s,\"servo_limits\":[%.1f,%.1f]}",
        c.servoPin, c.bottomDeg, c.topDeg, c.stepDeg, c.levelDeg, c.dir,
        static_cast<unsigned long>(c.settleMs), static_cast<unsigned long>(c.frameDelayMs), c.speedDps,
        c.holdSec, c.cameraHeightCm, c.focal, mac, camera, c.camFramesize, c.camQuality, ssid,
        c.password[0] ? "true" : "false", double(SWEEP_LIMIT_MIN_DEG), double(SWEEP_LIMIT_MAX_DEG));
    return n > 0 && static_cast<size_t>(n) < size;
}
