#pragma once
// Manual servo jog for calibration (servo_level, servo_dir, servo_bottom/top) over USB,
// without reflashing the Arduino servo_test env. Pure C++: tests/edge_native_plan2_test.cpp.
//   "SERVO 92.5"  move (smoothly) to 92.5 deg and hold; renew at least every 10 s
//                 (only SWEEP_LIMIT_MIN_DEG..MAX, default 45..135; outside -> Invalid)
//   "SERVO OFF"   release now
// Like PREVIEW, this is a RAM-only lease: losing the USB client detaches the servo and
// returns to Plan 2 after 10 s. The FSM, capture and scale wake are paused meanwhile.
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "../../../lib/EdgeSweep/edge_config.h"  // SWEEP_LIMIT_MIN_DEG / MAX (45..135)

enum class ServoJogCommand { None, Move, Off, Invalid };

// "SERVO 90" -> Move (deg = 90); "SERVO OFF" -> Off; other "SERVO ..." -> Invalid;
// lines not starting with "SERVO" -> None.
inline ServoJogCommand parseServoJog(const char* line, float& deg) {
    if (!line || std::strncmp(line, "SERVO", 5) != 0) return ServoJogCommand::None;
    const char* arg = line + 5;
    if (*arg != ' ') return *arg ? ServoJogCommand::None : ServoJogCommand::Invalid;
    while (*arg == ' ') ++arg;
    if (!std::strcmp(arg, "OFF")) return ServoJogCommand::Off;
    char* end = nullptr;
    const float v = std::strtof(arg, &end);
    if (end == arg || *end != '\0' || !std::isfinite(v) || v < SWEEP_LIMIT_MIN_DEG || v > SWEEP_LIMIT_MAX_DEG)
        return ServoJogCommand::Invalid;
    deg = v;
    return ServoJogCommand::Move;
}

class ServoJogLease {
public:
    static constexpr uint32_t timeoutMs = 10000;
    void renew(uint32_t now, float deg) {
        deci_.store(int32_t(std::lround(deg * 10.0f)));
        at_.store(now);
        enabled_.store(true);
    }
    void stop() { enabled_.store(false); }
    bool active(uint32_t now) const {
        return enabled_.load() && uint32_t(now - at_.load()) < timeoutMs;
    }
    float deg() const { return float(deci_.load()) / 10.0f; }  // 0.1 deg resolution
private:
    std::atomic<uint32_t> at_{0};
    std::atomic<int32_t> deci_{900};
    std::atomic<bool> enabled_{false};
};
