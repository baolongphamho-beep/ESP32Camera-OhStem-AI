// Native tilt-servo driver. Base: TV3 servo_ctrl.zip (22/11/2026). Changes for the LAB
// Plan 2 firmware (TV5):
//  - servoMoveDeg() is NON-blocking (the FSM loop keeps reading scale/USB); the caller
//    polls servoSettled(). The capture gate passes settle = servo_settle + servo_delay.
//  - the last angle survives servoDetach(): the next servoInit() resumes from it, so
//    homing after RESULT_HOLD is a smooth move instead of a full-speed snap.
//  - servoDetach() waits for an in-flight tick so no pulse is written after ledc_stop().
//  - Interface Contract extras: servoUpdate() (no-op, the esp_timer tick drives motion),
//    servoSettled(), servoAttached(), servoCommandDeg().
#include "servo_ctrl.h"
#include "edge_io.h"
#include "../../../lib/EdgeSweep/servo_motion.h"

#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "servo";

// LEDC resources. S3 only has the low-speed group. High indices are used on purpose
// to stay clear of anything else that might claim timer/channel 0.
static constexpr ledc_mode_t      kMode    = LEDC_LOW_SPEED_MODE;
static constexpr ledc_timer_t     kTimer   = LEDC_TIMER_3;
static constexpr ledc_channel_t   kChannel = LEDC_CHANNEL_5;
static constexpr ledc_timer_bit_t kBits    = LEDC_TIMER_14_BIT;
static constexpr uint32_t         kFreqHz  = 50;

static constexpr int64_t kTickUs   = 20000; // = one PWM period: exactly one new duty per pulse
static constexpr float   kHomeDps  = 90.f;  // return-to-start speed (continuous sweep)

enum class Phase : uint8_t { Detached, Holding, Moving, Homing, Sweeping, SweepDone };

static ServoMotion s_motion;
static ServoHistory s_hist;
static ServoPulse s_pulse;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static Phase s_phase = Phase::Detached;
static esp_timer_handle_t s_timer = nullptr;
static int s_pin = -1;
static bool s_inited = false, s_timerCfg = false, s_attached = false, s_known = false;
static float s_sweepDps = 15.f;
static uint32_t s_stillSince = 0, s_sweepStartedAt = 0, s_settleMs = 0;
static int64_t s_lastTickUs = 0;

static void writeDuty(float deg) {
    ledc_set_duty(kMode, kChannel, servoDuty(deg, s_pulse, 14));
    ledc_update_duty(kMode, kChannel);
}

// Runs every 20 ms in the esp_timer task: independent of the YOLO inference load on
// the main task, so the servo setpoint stays smooth.
static void tick(void*) {
    const int64_t t = esp_timer_get_time();
    float dt = float(t - s_lastTickUs) / 1e6f;
    s_lastTickUs = t;
    dt = std::min(0.08f, std::max(0.005f, dt));

    float pos;
    bool write;
    portENTER_CRITICAL(&s_mux);
    write = s_attached;  // servoDetach() clears it before stopping the PWM
    const bool done = write && s_motion.step(dt);
    if (done) {
        switch (s_phase) {
            case Phase::Homing:   // reached the start angle -> begin the sweep
                s_phase = Phase::Sweeping;
                s_sweepStartedAt = clockMs();
                s_motion.setTarget(s_motion.maxDeg(), s_sweepDps);
                break;
            case Phase::Sweeping: s_phase = Phase::SweepDone; s_stillSince = clockMs(); break;
            case Phase::Moving:   s_phase = Phase::Holding;   s_stillSince = clockMs(); break;
            default: break;
        }
    }
    pos = s_motion.pos();
    if (write) s_hist.push(clockMs(), pos);
    portEXIT_CRITICAL(&s_mux);
    if (write) writeDuty(pos);
}

static bool attach() {
    if (s_attached) return true;
    if (!s_timerCfg) {
        ledc_timer_config_t tc = {};
        tc.speed_mode = kMode;
        tc.duty_resolution = kBits;
        tc.timer_num = kTimer;
        tc.freq_hz = kFreqHz;
        tc.clk_cfg = LEDC_AUTO_CLK;
        if (ledc_timer_config(&tc) != ESP_OK) { ESP_LOGE(TAG, "LEDC timer config failed"); return false; }
        s_timerCfg = true;
    }
    float pos;
    portENTER_CRITICAL(&s_mux);
    pos = s_motion.pos();
    portEXIT_CRITICAL(&s_mux);

    ledc_channel_config_t cc = {};
    cc.gpio_num = s_pin;
    cc.speed_mode = kMode;
    cc.channel = kChannel;
    cc.timer_sel = kTimer;
    cc.duty = servoDuty(pos, s_pulse, 14); // first pulse already at the held angle: no glitch
    cc.hpoint = 0;
    if (ledc_channel_config(&cc) != ESP_OK) { ESP_LOGE(TAG, "LEDC channel config failed"); return false; }

    s_lastTickUs = esp_timer_get_time();
    portENTER_CRITICAL(&s_mux);
    s_attached = true;
    portEXIT_CRITICAL(&s_mux);
    if (esp_timer_start_periodic(s_timer, kTickUs) != ESP_OK) {
        ESP_LOGE(TAG, "tick timer failed");
        portENTER_CRITICAL(&s_mux);
        s_attached = false;
        portEXIT_CRITICAL(&s_mux);
        return false;
    }
    return true;
}

void servoAssumeAt(float deg) {
    if (!std::isfinite(deg)) return;
    portENTER_CRITICAL(&s_mux);
    if (!s_known) {
        s_motion.configure(SWEEP_LIMIT_MIN_DEG, SWEEP_LIMIT_MAX_DEG, kServoAccelDps2);
        s_motion.reset(deg);
        s_known = true;
    }
    portEXIT_CRITICAL(&s_mux);
}

bool servoInit(int gpioPin, int minDeg, int maxDeg) {
    // No reserved-pin blocking (team decision 22/11); an invalid GPIO fails in LEDC.
    if (gpioPin < 0 || gpioPin > 48 || minDeg < 0 || maxDeg > 180 || maxDeg - minDeg < 1) {
        ESP_LOGE(TAG, "bad args pin=%d range=[%d,%d]", gpioPin, minDeg, maxDeg);
        return false;
    }
    if (s_attached) servoDetach();
    s_pin = gpioPin;
    if (!s_timer) {
        esp_timer_create_args_t a = {};
        a.callback = tick;
        a.dispatch_method = ESP_TIMER_TASK;
        a.name = "servo_tick";
        if (esp_timer_create(&a, &s_timer) != ESP_OK) return false;
    }
    // Hard travel limits (edge_config.h, 45..135 by default) win over the caller's range.
    const float lo = std::max(float(minDeg), float(SWEEP_LIMIT_MIN_DEG));
    const float hi = std::min(float(maxDeg), float(SWEEP_LIMIT_MAX_DEG));
    if (hi - lo < 1.f) { ESP_LOGE(TAG, "range [%d,%d] outside hard limits", minDeg, maxDeg); return false; }
    portENTER_CRITICAL(&s_mux);
    const float last = s_motion.pos();
    s_motion.configure(lo, hi, kServoAccelDps2);
    // TV3 assumed the arm rests at minDeg. After a detach we still know where it was
    // left (gears hold it), so resume there; only the first attach after boot snaps.
    s_motion.reset(s_known ? last : lo);
    s_known = true;
    s_phase = Phase::Holding;
    s_stillSince = clockMs();
    s_sweepStartedAt = 0;
    portEXIT_CRITICAL(&s_mux);
    s_inited = true;
    if (!attach()) return false;
    ESP_LOGI(TAG, "ready: GPIO%d, %u Hz/14-bit, range [%d,%d] deg, pulse %.0f-%.0f us",
             gpioPin, unsigned(kFreqHz), minDeg, maxDeg, s_pulse.minUs, s_pulse.maxUs);
    return true;
}

void servoMoveDeg(float deg, uint32_t settleMs) {
    if (!s_inited || !std::isfinite(deg) || !attach()) return;
    portENTER_CRITICAL(&s_mux);
    s_settleMs = settleMs;
    if (std::fabs(s_motion.clampDeg(deg) - s_motion.target()) < 1e-3f && s_phase != Phase::Sweeping &&
        s_phase != Phase::Homing) {
        portEXIT_CRITICAL(&s_mux);
        return;  // Same target: keep the running move / settle clock.
    }
    s_phase = Phase::Moving;
    s_stillSince = 0;
    s_motion.setTarget(deg, kServoMoveDps);
    if (!s_motion.moving()) { s_phase = Phase::Holding; s_stillSince = clockMs(); }
    portEXIT_CRITICAL(&s_mux);
}

void servoUpdate() {}  // Motion runs in the 20 ms esp_timer tick.

void servoDetach() {
    if (!s_attached) return;
    portENTER_CRITICAL(&s_mux);
    s_attached = false;          // a tick running right now will not write any more
    s_motion.stop();
    s_phase = Phase::Detached;
    portEXIT_CRITICAL(&s_mux);
    esp_timer_stop(s_timer);
    vTaskDelay(pdMS_TO_TICKS(30)); // let an in-flight tick finish its ledc write
    ledc_stop(kMode, kChannel, 0); // idle low: no pulses -> no holding current, no jitter
}

bool servoSettled() {
    portENTER_CRITICAL(&s_mux);
    const bool still = s_attached && (s_phase == Phase::Holding || s_phase == Phase::SweepDone) &&
                       s_stillSince != 0;
    const uint32_t since = s_stillSince, settle = s_settleMs;
    portEXIT_CRITICAL(&s_mux);
    return still && clockMs() - since >= settle;
}

bool servoAttached() { return s_attached; }

float servoCommandDeg() {
    portENTER_CRITICAL(&s_mux);
    const float t = s_attached ? s_motion.target() : NAN;
    portEXIT_CRITICAL(&s_mux);
    return t;
}

bool servoSweepStart(float speedDps) {
    if (!s_inited || !attach()) return false;
    portENTER_CRITICAL(&s_mux);
    s_sweepDps = std::min(60.f, std::max(1.f, speedDps));
    s_stillSince = 0;
    if (std::fabs(s_motion.pos() - s_motion.minDeg()) > 0.5f) {
        s_phase = Phase::Homing;
        s_motion.setTarget(s_motion.minDeg(), kHomeDps);
    } else {
        s_phase = Phase::Sweeping;
        s_sweepStartedAt = clockMs();
        s_motion.setTarget(s_motion.maxDeg(), s_sweepDps);
    }
    portEXIT_CRITICAL(&s_mux);
    return true;
}

float servoStop() {
    float pos;
    portENTER_CRITICAL(&s_mux);
    const bool wasMoving = (s_phase == Phase::Moving || s_phase == Phase::Homing || s_phase == Phase::Sweeping);
    s_motion.stop(); // setpoint frozen right now; next tick re-writes the same duty
    pos = s_motion.pos();
    if (wasMoving) { s_phase = Phase::Holding; s_stillSince = clockMs(); }
    portEXIT_CRITICAL(&s_mux);
    return pos;
}

float servoDeg() {
    portENTER_CRITICAL(&s_mux);
    const float p = s_motion.pos();
    portEXIT_CRITICAL(&s_mux);
    return p;
}

ServoState servoState() {
    portENTER_CRITICAL(&s_mux);
    const Phase ph = s_phase;
    portEXIT_CRITICAL(&s_mux);
    switch (ph) {
        case Phase::Detached:  return ServoState::Detached;
        case Phase::Holding:   return ServoState::Holding;
        case Phase::Moving:
        case Phase::Homing:    return ServoState::Moving;
        case Phase::Sweeping:  return ServoState::Sweeping;
        default:               return ServoState::SweepDone;
    }
}

bool servoDegAt(uint32_t ms, float& deg) {
    portENTER_CRITICAL(&s_mux);
    const bool ok = s_hist.at(ms, deg);
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

uint32_t servoStillSinceMs() {
    portENTER_CRITICAL(&s_mux);
    const uint32_t t = s_stillSince;
    portEXIT_CRITICAL(&s_mux);
    return t;
}

uint32_t servoSweepStartedAtMs() {
    portENTER_CRITICAL(&s_mux);
    const uint32_t t = s_sweepStartedAt;
    portEXIT_CRITICAL(&s_mux);
    return t;
}
