// Standalone positional MG995 test, YOLO UNO D11 (GPIO38).
// Brown/black -> G; red -> regulated 5V; orange/yellow -> S at D11.
// Prefer an external 5V servo supply with enough current; join its GND
// to UNO GND. With external power, leave UNO's 5V pin disconnected.
// Remove mechanical load for the first test. Not for continuous-rotation servos.
// Upload: pio run -e yolo_uno_servo_test -t upload --upload-port COM11
// Replace COM11 with the actual USB port (unrelated to the D11 signal pin).

#include <Arduino.h>
#include <driver/ledc.h>

namespace {
constexpr int SERVO_PIN = D11;  // D11 = GPIO38; change to match the S header.
constexpr int MIN_ANGLE = 0;
constexpr int MAX_ANGLE = 180;
constexpr uint32_t PWM_HZ = 50;
constexpr uint32_t PWM_PERIOD_US = 1000000 / PWM_HZ;
constexpr uint32_t DUTY_MAX = (1U << 14) - 1;
constexpr ledc_channel_t CHANNEL = LEDC_CHANNEL_0;
constexpr uint32_t MOVE_WAIT_MS = 1000;  // Allow motion before reversing direction.

void writeAngle(int angle) {
    angle = constrain(angle, MIN_ANGLE, MAX_ANGLE);
    // Conservative 1000..2000us nominal range. Actual travel depends on
    // the servo's calibration; nominal 0..180 may not cover its full travel.
    const uint32_t pulseUs = 1000 + (angle * 1000) / 180;
    const uint32_t duty = (pulseUs * DUTY_MAX) / PWM_PERIOD_US;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, CHANNEL));
}

void moveTo(int target) {
    Serial.printf("Move to nominal %d degrees\n", target);
    // Send the destination immediately: no software speed ramp.
    writeAngle(target);
    delay(MOVE_WAIT_MS);
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(1500);  // No wait for Serial: also works without a USB host.
    Serial.println("MG995 test: D11 / GPIO38, direct targets, nominal 0..180 degrees");

    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_14_BIT;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = PWM_HZ;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel = {};
    channel.gpio_num = SERVO_PIN;
    channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = CHANNEL;
    channel.intr_type = LEDC_INTR_DISABLE;
    channel.timer_sel = LEDC_TIMER_0;
    channel.duty = (1500U * DUTY_MAX) / PWM_PERIOD_US;
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
    delay(2000);
}

void loop() {
    moveTo(MIN_ANGLE);
    moveTo(MAX_ANGLE);
}
