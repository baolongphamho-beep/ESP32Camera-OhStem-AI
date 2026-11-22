#include "servo_ctrl.h"
#include "servo_motion.h"

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
static constexpr float   kHomeDps  = 90.f;  // return-to-start speed
static constexpr float   kMoveDps  = 60.f;  // servoMoveDeg cruise speed
static constexpr float   kAccel    = 240.f; // deg/s^2: soft start/stop, no jerk on the Type-C cable

enum class Phase : uint8_t { Detached, Holding, Moving, Homing, Sweeping, SweepDone };

static ServoMotion s_motion;
static ServoHistory s_hist;
static ServoPulse s_pulse;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static Phase s_phase = Phase::Detached;
static esp_timer_handle_t s_timer = nullptr;
static int s_pin = -1;
static bool s_inited = false, s_timerCfg = false, s_attached = false;
static float s_sweepDps = 15.f;
static uint32_t s_stillSince = 0, s_sweepStartedAt = 0;
static int64_t s_lastTickUs = 0;

static uint32_t nowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL); }

static void writeDuty(float deg) {
    ledc_set_duty(kMode, kChannel, servoDuty(deg, s_pulse, 14));
    ledc_update_duty(kMode, kChannel);
}

// Runs every 20 ms in the esp_timer task (priority 22): independent of the YOLO
// inference load on the main task, so the servo setpoint stays smooth.
static void tick(void*) {
    const int64_t t = esp_timer_get_time();
    float dt = float(t - s_lastTickUs) / 1e6f;
    s_lastTickUs = t;
    dt = std::min(0.08f, std::max(0.005f, dt));

    float pos;
    portENTER_CRITICAL(&s_mux);
    const bool done = s_motion.step(dt);
    if (done) {
        switch (s_phase) {
            case Phase::Homing:   // reached the start angle -> begin the sweep
                s_phase = Phase::Sweeping;
                s_sweepStartedAt = nowMs();
                s_motion.setTarget(s_motion.maxDeg(), s_sweepDps);
                break;
            case Phase::Sweeping: s_phase = Phase::SweepDone; s_stillSince = nowMs(); break;
            case Phase::Moving:   s_phase = Phase::Holding;   s_stillSince = nowMs(); break;
            default: break;
        }
    }
    pos = s_motion.pos();
    s_hist.push(nowMs(), pos);
    portEXIT_CRITICAL(&s_mux);
    writeDuty(pos);
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
    cc.intr_type = LEDC_INTR_DISABLE;
    cc.timer_sel = kTimer;
    cc.duty = servoDuty(pos, s_pulse, 14); // first pulse already at the held angle: no glitch
    cc.hpoint = 0;
    if (ledc_channel_config(&cc) != ESP_OK) { ESP_LOGE(TAG, "LEDC channel config failed"); return false; }

    s_lastTickUs = esp_timer_get_time();
    if (esp_timer_start_periodic(s_timer, kTickUs) != ESP_OK) { ESP_LOGE(TAG, "tick timer failed"); return false; }
    s_attached = true;
    return true;
}

bool servoInit(int gpioPin, int minDeg, int maxDeg) {
    if (gpioPin < 0 || minDeg < 0 || maxDeg > 180 || maxDeg - minDeg < 5) {
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
    portENTER_CRITICAL(&s_mux);
    s_motion.configure(float(minDeg), float(maxDeg), kAccel);
    s_motion.reset(float(minDeg)); // rest position = bottom
    s_phase = Phase::Holding;
    s_stillSince = nowMs();
    s_sweepStartedAt = 0;
    portEXIT_CRITICAL(&s_mux);
    s_inited = true;
    if (!attach()) return false;
    ESP_LOGI(TAG, "ready: GPIO%d, %u Hz/14-bit, range [%d,%d] deg, pulse %.0f-%.0f us",
             gpioPin, unsigned(kFreqHz), minDeg, maxDeg, s_pulse.minUs, s_pulse.maxUs);
    return true;
}

void servoMoveDeg(float deg, uint32_t settleMs) {
    if (!s_inited || !attach()) return;
    float travel;
    portENTER_CRITICAL(&s_mux);
    s_phase = Phase::Moving;
    s_stillSince = 0;
    s_motion.setTarget(deg, kMoveDps);
    travel = std::fabs(s_motion.target() - s_motion.pos());
    if (!s_motion.moving()) { s_phase = Phase::Holding; s_stillSince = nowMs(); }
    portEXIT_CRITICAL(&s_mux);

    // Wait for arrival (or for someone else to stop/redirect us), bounded by the
    // expected travel time so a bad call can never hang the caller.
    const uint32_t budgetMs = uint32_t(travel / kMoveDps * 1000.f) + 1500;
    const uint32_t t0 = nowMs();
    while (nowMs() - t0 < budgetMs) {
        bool moving;
        portENTER_CRITICAL(&s_mux);
        moving = (s_phase == Phase::Moving);
        portEXIT_CRITICAL(&s_mux);
        if (!moving) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (settleMs) vTaskDelay(pdMS_TO_TICKS(settleMs)); // let the horn stop ringing
}

void servoDetach() {
    if (!s_attached) return;
    esp_timer_stop(s_timer);
    portENTER_CRITICAL(&s_mux);
    s_motion.stop();
    s_phase = Phase::Detached;
    portEXIT_CRITICAL(&s_mux);
    ledc_stop(kMode, kChannel, 0); // idle low: no pulses -> no holding current, no jitter
    s_attached = false;
}

float servoToTiltDeg(float servoDeg, const EdgeConfig& cfg) {
    return servoTiltFromDeg(servoDeg, cfg.servoLevelDeg, cfg.servoDir);
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
        s_sweepStartedAt = nowMs();
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
    if (wasMoving) { s_phase = Phase::Holding; s_stillSince = nowMs(); }
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
