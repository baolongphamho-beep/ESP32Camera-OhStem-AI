#pragma once
// Plan 2 "servo sweep" settings. Pure C++ (no Arduino): tested by
// tests/edge_sweep_logic_test.cpp. Keys and ranges match camera/usb_protocol.py
// (SERVO_LIMITS), so `usb_viewer.py --configure / --set` can change them at runtime.
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#ifndef SWEEP_SERVO_PIN
#define SWEEP_SERVO_PIN 10          // Plan 2 interface contract: GPIO10 (D7)
#endif
#ifndef SWEEP_BOTTOM_DEG
#define SWEEP_BOTTOM_DEG 0.0f       // servo_bottom: sweep starts here (camera looks down)
#endif
#ifndef SWEEP_TOP_DEG
#define SWEEP_TOP_DEG 180.0f        // servo_top: sweep ends here
#endif
#ifndef SWEEP_STEP_DEG
#define SWEEP_STEP_DEG 4.0f         // servo_step
#endif
#ifndef SWEEP_LEVEL_DEG
#define SWEEP_LEVEL_DEG 90.0f       // servo_level: command with the camera level
#endif
#ifndef SWEEP_DIR
#define SWEEP_DIR 1                 // servo_dir: tilt_down = dir * (level - command)
#endif
#ifndef SWEEP_SETTLE_MS
#define SWEEP_SETTLE_MS 400         // servo_settle: wait after a move before using a frame
#endif
#ifndef SWEEP_HOLD_SEC
#define SWEEP_HOLD_SEC 12.0f        // hold_sec: keep H/W/BMI after stepping off
#endif
#ifndef SWEEP_CAMERA_HEIGHT_CM
#define SWEEP_CAMERA_HEIGHT_CM 110.0f  // "height": lens above the scale top (cm)
#endif
#ifndef SWEEP_FOCAL
#define SWEEP_FOCAL 1.33f           // "focal": fy / image height (OV2640 3.6 mm at 320x240)
#endif

struct EdgeConfig {
    int servoPin = SWEEP_SERVO_PIN;
    float bottomDeg = SWEEP_BOTTOM_DEG;
    float topDeg = SWEEP_TOP_DEG;
    float stepDeg = SWEEP_STEP_DEG;
    float levelDeg = SWEEP_LEVEL_DEG;
    int dir = SWEEP_DIR;
    uint32_t settleMs = SWEEP_SETTLE_MS;
    float holdSec = SWEEP_HOLD_SEC;
    float cameraHeightCm = SWEEP_CAMERA_HEIGHT_CM;
    float focal = SWEEP_FOCAL;
    // "mac" (Plan 2 scaleMac): "AA:BB:CC:DD:EE:FF"; empty = SCALE_MAC from lib/Config/config.h.
    // Used by the BLE scan task, so a change takes effect after a reboot.
    char scaleMac[18] = "";
    // Not configurable over USB (build flags / code only):
    float marginFrac = 0.03f;        // safe border, fraction of the image (~7 px at 240 px)
    float wakeKg = 5.0f;             // weight that wakes the system
    int lockFrames = 3;              // full-body frames averaged (median) at the stop angle
    uint32_t lockTimeoutMs = 5000;   // no enough valid frames -> resume the sweep
    uint32_t weightTimeoutMs = 15000;// no scale lock after the height -> show H anyway
    uint32_t frameTimeoutMs = 3000;  // no frame after settling -> "Camera error"
    uint32_t noticeMs = 3000;        // how long "Step back!" / "No full body" stay
    uint32_t leaveGraceMs = 2000;    // weight gone this long = person left
    uint32_t homingMs = 800;         // time to reach servo_bottom from anywhere
    uint32_t holdMs() const { return (uint32_t)lroundf(holdSec * 1000.0f); }
};

enum class ConfigResult { Applied, Ignored, Invalid, Unknown };

namespace sweep_config_detail {
inline bool parseNumber(const char* text, float& out) {
    if (!text || !*text) return false;
    char* end = nullptr;
    const float v = std::strtof(text, &end);
    if (end == text || *end != '\0' || !std::isfinite(v)) return false;
    out = v;
    return true;
}
inline bool whole(float v) { return std::fabs(v - std::round(v)) < 1e-6f; }
// USB, flash/PSRAM, UART0, LCD I2C, camera UART of this firmware.
inline bool reservedPin(int pin, int camRx, int camTx) {
    return pin == 11 || pin == 12 || pin == 19 || pin == 20 || pin == 43 || pin == 44 ||
           (pin >= 26 && pin <= 37) || pin == camRx || pin == camTx;
}
// "70:87:9e:ea:3a:e0" -> "70:87:9E:EA:3A:E0"; false unless exactly 6 hex pairs with ':'.
inline bool normalizeMac(const char* in, char (&out)[18]) {
    if (!in || std::strlen(in) != 17) return false;
    for (int i = 0; i < 17; ++i) {
        const char c = in[i];
        if (i % 3 == 2) {
            if (c != ':') return false;
            out[i] = ':';
        } else if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) {
            out[i] = c;
        } else if (c >= 'a' && c <= 'f') {
            out[i] = (char)(c - 'a' + 'A');
        } else {
            return false;
        }
    }
    out[17] = '\0';
    return true;
}
}  // namespace sweep_config_detail

// Apply one "CFG key=value" to cfg. Keys of other firmwares (Wi-Fi, camera URL,
// fixed-mode geometry) are Ignored, not errors, so the same viewer works.
inline ConfigResult applyConfigKey(EdgeConfig& cfg, const char* key, const char* value,
                                   int camRx = -1, int camTx = -1) {
    using namespace sweep_config_detail;
    static const char* const ignored[] = {"ssid", "password", "camera", "mode",
                                          "distance", "reference", "tilt"};
    for (const char* k : ignored) if (std::strcmp(key, k) == 0) return ConfigResult::Ignored;
    if (!std::strcmp(key, "mac")) {
        char mac[18];
        if (!normalizeMac(value, mac)) return ConfigResult::Invalid;
        std::memcpy(cfg.scaleMac, mac, sizeof(mac));
        return ConfigResult::Applied;
    }
    float v = 0;
    const bool number = parseNumber(value, v);
    auto in = [&](float lo, float hi) { return number && v >= lo && v <= hi; };
    if (!std::strcmp(key, "servo_pin")) {
        if (!in(0, 48) || !whole(v) || reservedPin((int)v, camRx, camTx)) return ConfigResult::Invalid;
        cfg.servoPin = (int)v;
    } else if (!std::strcmp(key, "servo_bottom")) {
        if (!in(0, 180)) return ConfigResult::Invalid;
        cfg.bottomDeg = v;
    } else if (!std::strcmp(key, "servo_top")) {
        if (!in(0, 180)) return ConfigResult::Invalid;
        cfg.topDeg = v;
    } else if (!std::strcmp(key, "servo_step")) {
        if (!in(0.5f, 20)) return ConfigResult::Invalid;
        cfg.stepDeg = v;
    } else if (!std::strcmp(key, "servo_level")) {
        if (!in(0, 180)) return ConfigResult::Invalid;
        cfg.levelDeg = v;
    } else if (!std::strcmp(key, "servo_dir")) {
        if (!number || !(v == 1 || v == -1)) return ConfigResult::Invalid;
        cfg.dir = (int)v;
    } else if (!std::strcmp(key, "servo_settle")) {
        if (!in(0, 3000) || !whole(v)) return ConfigResult::Invalid;
        cfg.settleMs = (uint32_t)v;
    } else if (!std::strcmp(key, "hold_sec")) {
        if (!in(0, 120)) return ConfigResult::Invalid;
        cfg.holdSec = v;
    } else if (!std::strcmp(key, "height")) {
        if (!in(1, 400)) return ConfigResult::Invalid;
        cfg.cameraHeightCm = v;
    } else if (!std::strcmp(key, "focal")) {
        if (number && v == 0) return ConfigResult::Ignored;  // "auto" of fixed mode: n/a here
        if (!in(0.01f, 20)) return ConfigResult::Invalid;
        cfg.focal = v;
    } else {
        return ConfigResult::Unknown;
    }
    return ConfigResult::Applied;
}

// Whole-config check after a batch (cross-field rules).
inline bool configValid(const EdgeConfig& c) {
    return c.bottomDeg != c.topDeg && (c.dir == 1 || c.dir == -1) && c.stepDeg >= 0.5f &&
           c.focal > 0 && c.cameraHeightCm > 0;
}
