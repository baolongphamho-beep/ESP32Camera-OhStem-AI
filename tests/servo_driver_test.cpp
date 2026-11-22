#include <cassert>
#include <cmath>
#include "driver/ledc.h"
#include "servo_ctrl.h"
#include "servo_fake_state.h"

int main() {
    auto& st = fakeLedcState();
    st.reset();

    // 1. Initialize on GPIO 10: 50 Hz 14-bit XTAL timer + channel, detached initially.
    assert(servoInit(10, 40, 140));
    assert(st.timerConfigs.size() == 1);
    assert(st.timerConfigs[0].dutyResolution == 14);
    assert(st.timerConfigs[0].freqHz == 50U);
    assert(st.timerConfigs[0].clkCfg == static_cast<int>(LEDC_USE_XTAL_CLK));
    assert(st.channelConfigs.size() == 1);
    assert(st.channelConfigs[0].gpioNum == 10);
    assert(st.stopIdleLevels.size() == 1 && st.stopIdleLevels[0] == 0U);
    assert(st.pauseCalls == 1);
    assert(servoIsInitialized());
    assert(!servoIsAttached());
    assert(std::fabs(servoCurrentDeg() - 90.0f) < 0.001f);

    // 2. Rejecting a reserved USB pin must NOT reset or detach the working GPIO 10 channel.
    st.gpioResets.clear();
    assert(!servoInit(19, 40, 140));
    assert(st.gpioResets.empty());
    assert(servoIsInitialized());

    // 3. First move resets + resumes timer, writes duty via ledc_set_duty + ledc_update_duty,
    //    and enforces >= 250 ms initial settle wait.
    st.delaysMs.clear();
    st.resetCalls = 0;
    st.resumeCalls = 0;
    st.setDutyCalls.clear();
    st.updateDutyCalls = 0;
    servoMoveDeg(90.0f, 200);
    assert(st.resetCalls == 1);
    assert(st.resumeCalls == 1);
    assert(st.setDutyCalls.size() == 1 && st.setDutyCalls[0] == 1229U);
    assert(st.updateDutyCalls == 1);
    assert(servoIsAttached());
    assert(std::fabs(servoCurrentDeg() - 90.0f) < 0.001f);
    assert(st.delaysMs.size() == 1 && st.delaysMs[0] == 250U);

    // 4. Ramped move from 90 -> 100 deg: 5 steps <= 2 deg, four 20 ms ramp delays + 150 ms settle.
    st.delaysMs.clear();
    st.resetCalls = 0;
    st.resumeCalls = 0;
    st.setDutyCalls.clear();
    st.updateDutyCalls = 0;
    servoMoveDeg(100.0f, 150);
    assert(st.resetCalls == 0);
    assert(st.resumeCalls == 0);
    assert(st.setDutyCalls.size() == 5);
    assert(st.updateDutyCalls == 5);
    assert(std::fabs(servoCurrentDeg() - 100.0f) < 0.001f);
    assert(st.delaysMs.size() == 5);
    for (int i = 0; i < 4; ++i) {
        assert(st.delaysMs[i] == 20U);
    }
    assert(st.delaysMs[4] == 150U);

    // 5. Out-of-range angle clamps to maxDeg (140 deg).
    st.delaysMs.clear();
    st.setDutyCalls.clear();
    servoMoveDeg(175.0f, 0);
    assert(std::fabs(servoCurrentDeg() - 140.0f) < 0.001f);
    assert(!st.setDutyCalls.empty());
    assert(st.setDutyCalls.back() == servoPulseUsToDuty(servoDegToPulseUs(140.0f), 50U, 14U));

    // 6. servoDetach() stops PWM at idle level 0 and pauses timer.
    st.stopIdleLevels.clear();
    st.pauseCalls = 0;
    servoDetach();
    assert(st.stopIdleLevels.size() == 1 && st.stopIdleLevels[0] == 0U);
    assert(st.pauseCalls == 1);
    assert(!servoIsAttached());

    // 7. Moving again after detach calls ledc_timer_rst + ledc_timer_resume + duty update.
    st.resetCalls = 0;
    st.resumeCalls = 0;
    st.setDutyCalls.clear();
    st.updateDutyCalls = 0;
    servoMoveDeg(138.0f, 0);
    assert(st.resetCalls == 1);
    assert(st.resumeCalls == 1);
    assert(st.setDutyCalls.size() == 1);
    assert(st.updateDutyCalls == 1);
    assert(servoIsAttached());
    assert(std::fabs(servoCurrentDeg() - 138.0f) < 0.001f);

    // 8. LEDC failure must abort move without advancing position or waiting settleMs.
    st.delaysMs.clear();
    st.failSetDuty = true;
    servoMoveDeg(130.0f, 50);
    assert(std::fabs(servoCurrentDeg() - 138.0f) < 0.001f);
    assert(st.delaysMs.empty());
    st.failSetDuty = false;

    servoDetach();
    st.delaysMs.clear();
    st.failResume = true;
    servoMoveDeg(120.0f, 50);
    assert(!servoIsAttached());
    assert(std::fabs(servoCurrentDeg() - 138.0f) < 0.001f);
    assert(st.delaysMs.empty());
    st.failResume = false;

    // 9. Disabling servo with pin=-1 detaches, resets GPIO 10, and makes moves no-op.
    st.gpioResets.clear();
    st.setDutyCalls.clear();
    assert(!servoInit(-1, 40, 140));
    assert(st.gpioResets.size() == 1 && st.gpioResets[0] == 10);
    assert(!servoIsInitialized());
    assert(!servoIsAttached());
    servoMoveDeg(90.0f, 100);
    assert(st.setDutyCalls.empty());

    // 10. servoInitFromConfig() seeds servoCurrentDeg() from servoLevelDeg and computes tilt.
    EdgeConfig cfg;
    cfg.servoPin = 10;
    cfg.servoMinDeg = 45.0f;
    cfg.servoMaxDeg = 135.0f;
    cfg.servoLevelDeg = 88.0f;
    cfg.servoDir = -1;
    cfg.tiltDeg = 0.0f;
    assert(servoInitFromConfig(cfg));
    assert(servoIsInitialized());
    assert(!servoIsAttached());
    assert(std::fabs(servoCurrentDeg() - 88.0f) < 0.001f);
    assert(std::fabs(servoToTiltDeg(68.0f, cfg) - 20.0f) < 0.001f);
    return 0;
}
