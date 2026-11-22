#include "servo_ctrl.h"
#include "servo_math.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char* TAG = "servo_ctrl";

static constexpr ledc_mode_t kLedcMode = LEDC_LOW_SPEED_MODE;
static constexpr ledc_timer_t kLedcTimer = LEDC_TIMER_0;
static constexpr ledc_channel_t kLedcChannel = LEDC_CHANNEL_0;

static SemaphoreHandle_t s_servo_mutex = nullptr;
static std::once_flag s_mutex_once;

static int s_gpio = -1;
static float s_min_deg = kServoDefaultMinDeg;
static float s_max_deg = kServoDefaultMaxDeg;
static float s_current_deg = kServoDefaultLevelDeg;
static bool s_initialized = false;
static bool s_channel_configured = false;
static bool s_attached = false;
static bool s_has_position = false;
static std::atomic<bool> s_initialized_atomic{false};
static std::atomic<bool> s_attached_atomic{false};
static std::atomic<float> s_current_deg_atomic{kServoDefaultLevelDeg};

static void ensureServoMutex() {
    std::call_once(s_mutex_once, []() {
        s_servo_mutex = xSemaphoreCreateMutex();
    });
}

static void servoDetachLocked() {
    if (s_channel_configured) {
        ledc_stop(kLedcMode, kLedcChannel, 0);
        ledc_timer_pause(kLedcMode, kLedcTimer);
    }
    s_attached = false;
    s_attached_atomic.store(false);
}

static bool servoApplyDegLocked(float deg) {
    const float pulseUs = servoDegToPulseUs(deg);
    const uint32_t duty = servoPulseUsToDuty(pulseUs, kServoPwmFreqHz, kServoDutyResolutionBits);
    if (!s_attached) {
        esp_err_t rc = ledc_timer_rst(kLedcMode, kLedcTimer);
        if (rc == ESP_OK) {
            rc = ledc_timer_resume(kLedcMode, kLedcTimer);
        }
        if (rc != ESP_OK) {
            ESP_LOGE(TAG, "LEDC timer resume failed: %s", esp_err_to_name(rc));
            return false;
        }
    }
    // Use ledc_set_duty() + ledc_update_duty() instead of ledc_set_duty_and_update(),
    // which requires ledc_fade_func_install() and otherwise returns ESP_FAIL.
    esp_err_t rc = ledc_set_duty(kLedcMode, kLedcChannel, duty);
    if (rc == ESP_OK) {
        rc = ledc_update_duty(kLedcMode, kLedcChannel);
    }
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "LEDC duty update failed: %s", esp_err_to_name(rc));
        return false;
    }
    s_attached = true;
    s_attached_atomic.store(true);
    return true;
}

static bool servoInitInternal(int gpioPin, float minDeg, float maxDeg, float initialDeg) {
    if (gpioPin != -1 && !isValidServoPin(gpioPin)) {
        ESP_LOGW(TAG, "Rejected invalid or reserved servo GPIO %d", gpioPin);
        return false;
    }
    ensureServoMutex();
    if (!s_servo_mutex) return false;
    xSemaphoreTake(s_servo_mutex, portMAX_DELAY);

    if (s_channel_configured && s_gpio != gpioPin) {
        servoDetachLocked();
        gpio_reset_pin(static_cast<gpio_num_t>(s_gpio));
        s_channel_configured = false;
    }

    if (gpioPin == -1) {
        servoDetachLocked();
        s_gpio = -1;
        s_initialized = false;
        s_initialized_atomic.store(false);
        s_has_position = false;
        xSemaphoreGive(s_servo_mutex);
        ESP_LOGI(TAG, "Servo disabled (pin=-1)");
        return false;
    }

    float lo = minDeg;
    float hi = maxDeg;
    if (!std::isfinite(lo) || !std::isfinite(hi)) {
        lo = kServoDefaultMinDeg;
        hi = kServoDefaultMaxDeg;
    } else {
        lo = std::clamp(lo, kServoHardMinDeg, kServoHardMaxDeg);
        hi = std::clamp(hi, kServoHardMinDeg, kServoHardMaxDeg);
        if (lo >= hi) {
            lo = kServoDefaultMinDeg;
            hi = kServoDefaultMaxDeg;
        }
    }

    if (!s_channel_configured) {
        ledc_timer_config_t timerCfg = {};
        timerCfg.speed_mode = kLedcMode;
        timerCfg.duty_resolution = LEDC_TIMER_14_BIT;
        timerCfg.timer_num = kLedcTimer;
        timerCfg.freq_hz = kServoPwmFreqHz;
        timerCfg.clk_cfg = LEDC_USE_XTAL_CLK;
        timerCfg.deconfigure = false;
        esp_err_t rc = ledc_timer_config(&timerCfg);
        if (rc != ESP_OK) {
            xSemaphoreGive(s_servo_mutex);
            ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(rc));
            return false;
        }

        ledc_channel_config_t chCfg = {};
        chCfg.gpio_num = gpioPin;
        chCfg.speed_mode = kLedcMode;
        chCfg.channel = kLedcChannel;
        chCfg.intr_type = LEDC_INTR_DISABLE;
        chCfg.timer_sel = kLedcTimer;
        chCfg.duty = 0;
        chCfg.hpoint = 0;
        chCfg.sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD;
        rc = ledc_channel_config(&chCfg);
        if (rc != ESP_OK) {
            xSemaphoreGive(s_servo_mutex);
            ESP_LOGE(TAG, "ledc_channel_config failed: %s", esp_err_to_name(rc));
            return false;
        }
        s_channel_configured = true;
    }

    servoDetachLocked();
    s_gpio = gpioPin;
    s_min_deg = lo;
    s_max_deg = hi;
    s_current_deg = clampServoDeg(initialDeg, lo, hi);
    s_current_deg_atomic.store(s_current_deg);
    s_initialized = true;
    s_initialized_atomic.store(true);
    s_has_position = false;
    xSemaphoreGive(s_servo_mutex);

    ESP_LOGI(TAG, "Servo initialized on GPIO %d (limits %.1f..%.1f deg)",
             gpioPin, lo, hi);
    return true;
}

bool servoInit(int gpioPin, int minDeg, int maxDeg) {
    const float initialDeg = 0.5f * (static_cast<float>(minDeg) + static_cast<float>(maxDeg));
    return servoInitInternal(gpioPin, static_cast<float>(minDeg), static_cast<float>(maxDeg), initialDeg);
}

bool servoInitFromConfig(const EdgeConfig& cfg) {
    return servoInitInternal(cfg.servoPin, cfg.servoMinDeg, cfg.servoMaxDeg, cfg.servoLevelDeg);
}

void servoMoveDeg(float deg, uint32_t settleMs) {
    ensureServoMutex();
    if (!s_servo_mutex) return;
    xSemaphoreTake(s_servo_mutex, portMAX_DELAY);
    if (!s_initialized || !s_channel_configured) {
        xSemaphoreGive(s_servo_mutex);
        return;
    }

    const float targetDeg = clampServoDeg(deg, s_min_deg, s_max_deg);
    uint32_t finalWaitMs = settleMs;
    bool ok = true;
    if (s_has_position) {
        const float startDeg = s_current_deg;
        const uint32_t steps = computeServoRampSteps(startDeg, targetDeg, kServoRampMaxStepDeg);
        if (steps == 0U) {
            ok = servoApplyDegLocked(targetDeg);
            if (ok) {
                s_current_deg = targetDeg;
                s_current_deg_atomic.store(targetDeg);
            }
        } else {
            for (uint32_t i = 1; i <= steps; ++i) {
                const float stepTarget = (i == steps)
                    ? targetDeg
                    : (startDeg + (targetDeg - startDeg) * (static_cast<float>(i) / static_cast<float>(steps)));
                if (!servoApplyDegLocked(stepTarget)) {
                    ok = false;
                    break;
                }
                s_current_deg = stepTarget;
                s_current_deg_atomic.store(stepTarget);
                if (i < steps) {
                    vTaskDelay(pdMS_TO_TICKS(kServoRampStepDelayMs));
                }
            }
        }
    } else {
        ok = servoApplyDegLocked(targetDeg);
        if (ok) {
            s_current_deg = targetDeg;
            s_current_deg_atomic.store(targetDeg);
            s_has_position = true;
            finalWaitMs = std::max<uint32_t>(settleMs, 250U);
        }
    }
    xSemaphoreGive(s_servo_mutex);

    if (ok && finalWaitMs > 0U) {
        vTaskDelay(pdMS_TO_TICKS(finalWaitMs));
    }
}

void servoDetach() {
    ensureServoMutex();
    if (!s_servo_mutex) return;
    xSemaphoreTake(s_servo_mutex, portMAX_DELAY);
    servoDetachLocked();
    xSemaphoreGive(s_servo_mutex);
}

bool servoIsInitialized() {
    return s_initialized_atomic.load();
}

bool servoIsAttached() {
    return s_attached_atomic.load();
}

float servoCurrentDeg() {
    return s_current_deg_atomic.load();
}

float servoToTiltDeg(float servoDeg, const EdgeConfig& cfg) {
    return servoDegToTiltDeg(servoDeg, cfg.servoLevelDeg, cfg.servoDir, cfg.tiltDeg);
}
