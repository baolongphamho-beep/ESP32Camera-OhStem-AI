#pragma once
#include <cstdint>

// Pure on/off logic for the scale supply (Project 5). Times are millis() and
// wrap safely. Kept free of Arduino calls so tests/scale_power_test.cpp can run on a PC.
enum class ScalePowerMode { Auto, ForcedOn, ForcedOff };

class ScalePowerState {
public:
    ScalePowerState(uint32_t idleOffMs, uint32_t minToggleMs)
        : idleOffMs_(idleOffMs), minToggleMs_(minToggleMs) {}

    // Power on at boot so the scale works immediately.
    void begin(uint32_t now) {
        on_ = true;
        lastActivity_ = now;
        lastToggle_ = now;
    }

    // activity: camera sees someone, or the scale is sending fresh weight data.
    // Returns true when the output must change.
    bool update(uint32_t now, bool activity) {
        if (activity) lastActivity_ = now;
        bool want = on_;
        if (mode_ == ScalePowerMode::ForcedOn) {
            want = true;
        } else if (mode_ == ScalePowerMode::ForcedOff) {
            want = false;
        } else if (activity) {
            want = true;
        } else if (now - lastActivity_ >= idleOffMs_) {
            want = false;
        }
        // Anti-chatter: a relay/MOSFET must not toggle faster than minToggleMs,
        // except when the user forces a state from the Serial Monitor.
        if (want != on_ && (mode_ != ScalePowerMode::Auto || now - lastToggle_ >= minToggleMs_)) {
            on_ = want;
            lastToggle_ = now;
            return true;
        }
        return false;
    }

    void setMode(uint32_t now, ScalePowerMode mode) {
        mode_ = mode;
        if (mode == ScalePowerMode::Auto) lastActivity_ = now;  // Auto restarts the idle timer.
    }

    bool isOn() const { return on_; }
    ScalePowerMode mode() const { return mode_; }

private:
    uint32_t idleOffMs_, minToggleMs_;
    uint32_t lastActivity_ = 0, lastToggle_ = 0;
    bool on_ = false;
    ScalePowerMode mode_ = ScalePowerMode::Auto;
};
