#pragma once
// Plan 2 Interface Contract 3.3 (edge_io.h) - LCD part, owner TV5 (TV5-2).
//   void lcdSetBacklight(bool on);
//   void lcdShowPhase(SystemPhase phase, EdgeNotice notice, const LcdData& data);
// The power/BLE half of the contract (setPowerMode, bleSetLowPowerScan) belongs to TV2 and
// is not in this PlatformIO build: here the scale task (lib/Scale) always scans.
//
// lcdTextFor() is pure C++, tested by tests/edge_sweep_logic_test.cpp (every line is
// exactly 16 chars, padded with spaces). The functions above live in edge_io.cpp (board
// only) and write a row only when its text changes (no clear, no flicker).
//
//   phase / notice        line 1             line 2
//   STANDBY               Standby            Step on scale
//   WAKING_HOMING         Waking up...       Please wait
//   SERVO_SWEEP           Aligning cam...    Stand still
//   LOCKED (no H yet)     Measuring...       Stand still
//   LOCKED (H, no lock)   H:170.2cm          Wait scale lock / W:65.3kg wait
//   RESULT_HOLD + BMI     H:170.2 W:65.3     BMI:22.5 D:2.8m
//   RESULT_HOLD, no BMI   H:170.2cm          No weight lock / W:65.3 unlocked
//   TooClose              Step back!         Too close to cam
//   Multiple              One at a time      2+ people seen
//   NoFullBody            No full body       Stand straight
//   CameraError           Camera error       Check laptop/IP
#include <cstdio>
#include <cstring>

#include "edge_fsm.h"

struct LcdData {
    bool hasHeight = false;
    float heightCm = 0;
    bool hasWeight = false;
    float weightKg = 0;
    bool weightStable = false;
    bool hasBmi = false;
    float bmi = 0;
    float distanceM = 0;
};

struct LcdLines {
    char line[2][17];
};

namespace edge_io_detail {
inline void put(char (&dst)[17], const char* text) {
    std::snprintf(dst, sizeof(dst), "%-16.16s", text ? text : "");
}
inline void putf(char (&dst)[17], const char* fmt, float a, float b = 0) {
    char tmp[40];
    std::snprintf(tmp, sizeof(tmp), fmt, (double)a, (double)b);
    put(dst, tmp);
}
// Keep numbers short enough for 16 columns.
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace edge_io_detail

inline LcdLines lcdTextFor(SystemPhase phase, EdgeNotice notice, const LcdData& d) {
    using namespace edge_io_detail;
    LcdLines out;
    const float h = clampf(d.heightCm, 0, 999.9f), w = clampf(d.weightKg, 0, 999.9f);
    // Warnings win, except over a finished result.
    if (phase != SystemPhase::ResultHold && phase != SystemPhase::Standby) {
        switch (notice) {
            case EdgeNotice::TooClose: put(out.line[0], "Step back!"); put(out.line[1], "Too close to cam"); return out;
            case EdgeNotice::Multiple: put(out.line[0], "One at a time"); put(out.line[1], "2+ people seen"); return out;
            case EdgeNotice::NoFullBody: put(out.line[0], "No full body"); put(out.line[1], "Stand straight"); return out;
            case EdgeNotice::CameraError: put(out.line[0], "Camera error"); put(out.line[1], "Check laptop/IP"); return out;
            case EdgeNotice::None: break;
        }
    }
    switch (phase) {
        case SystemPhase::Standby:
            put(out.line[0], "Standby"); put(out.line[1], "Step on scale");
            break;
        case SystemPhase::WakingHoming:
            put(out.line[0], "Waking up..."); put(out.line[1], "Please wait");
            break;
        case SystemPhase::ServoSweep:
            put(out.line[0], "Aligning cam..."); put(out.line[1], "Stand still");
            break;
        case SystemPhase::LockedMeasure:
            if (!d.hasHeight) {
                put(out.line[0], "Measuring..."); put(out.line[1], "Stand still");
            } else {
                putf(out.line[0], "H:%.1fcm", h);
                if (d.hasWeight) putf(out.line[1], "W:%.1fkg wait", w);
                else put(out.line[1], "Wait scale lock");
            }
            break;
        case SystemPhase::ResultHold:
            if (d.hasBmi && d.hasHeight && d.hasWeight) {
                putf(out.line[0], "H:%.1f W:%.1f", h, w);
                if (d.bmi >= 100) putf(out.line[1], "BMI:99+ D:%.1fm", clampf(d.distanceM, 0, 99.9f));
                else putf(out.line[1], "BMI:%.1f D:%.1fm", clampf(d.bmi, 0, 99.9f), clampf(d.distanceM, 0, 99.9f));
            } else if (d.hasHeight) {
                putf(out.line[0], "H:%.1fcm", h);
                if (d.hasWeight) putf(out.line[1], "W:%.1f unlocked", w);
                else put(out.line[1], "No weight lock");
            } else {
                put(out.line[0], "No result"); put(out.line[1], "Step on scale");
            }
            break;
    }
    return out;
}

// Board side (edge_io.cpp, env:yolo_uno_servo_sweep).
void lcdSetBacklight(bool on);
// Shows lcdTextFor(phase, notice, data); returns the lines written (for logs / USB status).
LcdLines lcdShowPhase(SystemPhase phase, EdgeNotice notice, const LcdData& data);
