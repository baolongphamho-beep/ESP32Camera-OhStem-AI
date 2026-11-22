#pragma once
#include <cstdint>
#include "esp_err.h"
#include "servo_fake_state.h"

typedef enum {
    LEDC_LOW_SPEED_MODE = 0,
    LEDC_SPEED_MODE_MAX,
} ledc_mode_t;

typedef enum {
    LEDC_TIMER_0 = 0,
    LEDC_TIMER_MAX,
} ledc_timer_t;

typedef enum {
    LEDC_CHANNEL_0 = 0,
    LEDC_CHANNEL_MAX,
} ledc_channel_t;

typedef enum {
    LEDC_TIMER_14_BIT = 14,
} ledc_timer_bit_t;

typedef enum {
    LEDC_USE_XTAL_CLK = 0,
} ledc_clk_cfg_t;

typedef enum {
    LEDC_INTR_DISABLE = 0,
} ledc_intr_type_t;

typedef enum {
    LEDC_SLEEP_MODE_NO_ALIVE_NO_PD = 0,
} ledc_sleep_mode_t;

typedef struct {
    ledc_mode_t speed_mode;
    ledc_timer_bit_t duty_resolution;
    ledc_timer_t timer_num;
    uint32_t freq_hz;
    ledc_clk_cfg_t clk_cfg;
    bool deconfigure;
} ledc_timer_config_t;

typedef struct {
    int gpio_num;
    ledc_mode_t speed_mode;
    ledc_channel_t channel;
    ledc_intr_type_t intr_type;
    ledc_timer_t timer_sel;
    uint32_t duty;
    int hpoint;
    ledc_sleep_mode_t sleep_mode;
} ledc_channel_config_t;

inline esp_err_t ledc_timer_config(const ledc_timer_config_t* cfg) {
    auto& st = fakeLedcState();
    if (st.failTimerConfig || !cfg) return ESP_FAIL;
    st.timerConfigs.push_back({
        static_cast<int>(cfg->speed_mode),
        static_cast<int>(cfg->duty_resolution),
        static_cast<int>(cfg->timer_num),
        cfg->freq_hz,
        static_cast<int>(cfg->clk_cfg),
        cfg->deconfigure,
    });
    return ESP_OK;
}

inline esp_err_t ledc_channel_config(const ledc_channel_config_t* cfg) {
    auto& st = fakeLedcState();
    if (st.failChannelConfig || !cfg) return ESP_FAIL;
    st.channelConfigs.push_back({
        cfg->gpio_num,
        static_cast<int>(cfg->speed_mode),
        static_cast<int>(cfg->channel),
        static_cast<int>(cfg->intr_type),
        static_cast<int>(cfg->timer_sel),
        cfg->duty,
        cfg->hpoint,
        static_cast<int>(cfg->sleep_mode),
    });
    return ESP_OK;
}

inline esp_err_t ledc_stop(ledc_mode_t, ledc_channel_t, uint32_t idle_level) {
    fakeLedcState().stopIdleLevels.push_back(idle_level);
    return ESP_OK;
}

inline esp_err_t ledc_timer_pause(ledc_mode_t, ledc_timer_t) {
    ++fakeLedcState().pauseCalls;
    return ESP_OK;
}

inline esp_err_t ledc_timer_rst(ledc_mode_t, ledc_timer_t) {
    ++fakeLedcState().resetCalls;
    return ESP_OK;
}

inline esp_err_t ledc_timer_resume(ledc_mode_t, ledc_timer_t) {
    auto& st = fakeLedcState();
    if (st.failResume) return ESP_FAIL;
    ++st.resumeCalls;
    return ESP_OK;
}

inline esp_err_t ledc_set_duty(ledc_mode_t, ledc_channel_t, uint32_t duty) {
    auto& st = fakeLedcState();
    if (st.failSetDuty) return ESP_FAIL;
    st.setDutyCalls.push_back(duty);
    return ESP_OK;
}

inline esp_err_t ledc_update_duty(ledc_mode_t, ledc_channel_t) {
    auto& st = fakeLedcState();
    if (st.failUpdateDuty) return ESP_FAIL;
    ++st.updateDutyCalls;
    return ESP_OK;
}
