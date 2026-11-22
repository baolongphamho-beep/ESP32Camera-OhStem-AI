#pragma once
#include <algorithm>
#include <cstdint>

static constexpr uint32_t kControlWaitForeverMs = UINT32_MAX;
static constexpr uint32_t kControlReportIntervalMs = 1000;
static constexpr uint32_t kControlDrainingPollMs = 100;
static constexpr uint32_t kControlActivePollMs = 250;

struct ControlScheduleInput {
    uint32_t nowMs = 0;
    uint32_t lastReportAtMs = 0;
    uint32_t holdAtMs = 0;
    uint32_t holdDurationMs = 0;
    bool active = false;
    bool powerActive = false;
    bool holding = false;
    bool usbConnected = false;
};

inline uint32_t remainingUntilMs(uint32_t nowMs, uint32_t startMs, uint32_t durationMs) {
    const uint32_t elapsed = nowMs - startMs;
    return (elapsed >= durationMs) ? 0U : (durationMs - elapsed);
}

inline uint32_t computeControlWaitMs(const ControlScheduleInput& in) {
    const uint32_t reportRem = remainingUntilMs(in.nowMs, in.lastReportAtMs, kControlReportIntervalMs);
    if (in.active) {
        return std::max<uint32_t>(1U, in.usbConnected ? std::min(reportRem, kControlActivePollMs) : kControlActivePollMs);
    }
    if (in.powerActive) {
        return std::max<uint32_t>(1U, in.usbConnected ? std::min(reportRem, kControlDrainingPollMs) : kControlDrainingPollMs);
    }
    if (in.holding) {
        const uint32_t holdRem = remainingUntilMs(in.nowMs, in.holdAtMs, in.holdDurationMs);
        if (holdRem == 0U) return 0U;
        return in.usbConnected ? std::max<uint32_t>(1U, std::min(holdRem, reportRem)) : holdRem;
    }
    if (!in.usbConnected) {
        return kControlWaitForeverMs;
    }
    return std::max<uint32_t>(1U, reportRem);
}
