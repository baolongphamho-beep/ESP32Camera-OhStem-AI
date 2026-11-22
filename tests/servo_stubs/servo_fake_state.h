#pragma once
#include <cstdint>
#include <vector>

struct FakeTimerRecord {
    int speedMode = 0;
    int dutyResolution = 0;
    int timerNum = 0;
    uint32_t freqHz = 0;
    int clkCfg = 0;
    bool deconfigure = false;
};

struct FakeChannelRecord {
    int gpioNum = -1;
    int speedMode = 0;
    int channel = 0;
    int intrType = 0;
    int timerSel = 0;
    uint32_t duty = 0;
    int hpoint = 0;
    int sleepMode = 0;
};

struct FakeLedcState {
    std::vector<FakeTimerRecord> timerConfigs;
    std::vector<FakeChannelRecord> channelConfigs;
    std::vector<uint32_t> setDutyCalls;
    int updateDutyCalls = 0;
    std::vector<uint32_t> stopIdleLevels;
    int pauseCalls = 0;
    int resetCalls = 0;
    int resumeCalls = 0;
    std::vector<int> gpioResets;
    std::vector<uint32_t> delaysMs;

    bool failSetDuty = false;
    bool failUpdateDuty = false;
    bool failResume = false;
    bool failTimerConfig = false;
    bool failChannelConfig = false;

    void reset() {
        *this = FakeLedcState{};
    }
};

inline FakeLedcState& fakeLedcState() {
    static FakeLedcState state;
    return state;
}
