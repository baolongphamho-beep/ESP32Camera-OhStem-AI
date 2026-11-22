#include "edge_io.h"
#include "ble_scan_retry.h"
#include "wifi_reconnect.h"
#include "wifi_credentials.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "esp_private/esp_clk.h"
#include "esp_rom_crc.h"
#include "esp_rom_sys.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_coexist.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "driver/usb_serial_jtag.h"

#if __has_include("driver/i2c_master.h")
#include "driver/i2c_master.h"
#define USE_I2C_MASTER_NEW 1
#elif __has_include("driver/i2c.h")
#include "driver/i2c.h"
#define USE_I2C_MASTER_LEGACY 1
#endif

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"

static const char* TAG = "edge_io";

// =========================================================================
// 1. Clock & Timing
// =========================================================================
uint32_t clockMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

// =========================================================================
// 2. Global State & Measurement Machine
// =========================================================================
static MeasurementState s_measurements;
static ScalePresence s_scale_presence;
static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
static std::atomic<bool> s_power_active{true};
static esp_pm_lock_handle_t s_active_cpu_lock = nullptr;
static esp_pm_lock_handle_t s_active_sleep_lock = nullptr;
static esp_pm_lock_handle_t s_usb_sleep_lock = nullptr;
static std::atomic<bool> s_usb_sleep_blocked{true};
static SemaphoreHandle_t s_control_wake_sem = nullptr;
static SemaphoreHandle_t s_lcd_wake_sem = nullptr;
static std::atomic<bool> s_lcd_backlight_state{true};
static std::atomic<uint32_t> s_wifi_connect_epoch{0};
static void initPowerManagement();
static void powerMonitorTask(void* arg);

bool usbConnected() {
    return usb_serial_jtag_is_connected();
}

uint32_t wifiConnectEpoch() {
    return s_wifi_connect_epoch.load();
}

void edgeNotifyControl() {
    if (s_control_wake_sem) {
        xSemaphoreGive(s_control_wake_sem);
    }
}

bool edgeWaitControl(uint32_t timeoutMs) {
    if (!s_control_wake_sem) {
        vTaskDelay(pdMS_TO_TICKS(timeoutMs == UINT32_MAX ? 100U : timeoutMs));
        return false;
    }
    const TickType_t ticks = (timeoutMs == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
    return xSemaphoreTake(s_control_wake_sem, ticks) == pdTRUE;
}

static void applyHoldSec(uint16_t holdSec) {
    portENTER_CRITICAL(&s_state_mux);
    s_measurements.setHoldMs(static_cast<uint32_t>(holdSec) * 1000U);
    portEXIT_CRITICAL(&s_state_mux);
}

MeasurementView scaleSnapshot() {
    return scaleSampleSnapshot().measurement;
}

ScaleSample scaleSampleSnapshot() {
    portENTER_CRITICAL(&s_state_mux);
    const auto view = s_scale_presence.snapshot(clockMs());
    portEXIT_CRITICAL(&s_state_mux);
    return view;
}

static void updateCameraLocked(PersonStatus status, float height, float distance) {
    const uint32_t now = clockMs();
    s_measurements.camera(now, status, height, distance);
    const auto scale = s_scale_presence.snapshot(now);
    if (status != PersonStatus::None && status != PersonStatus::Multiple && scale.present()) {
        s_measurements.weight(now, scale.measurement.weightKg, scale.measurement.weightStable);
    } else {
        s_measurements.clearWeight();
    }
}

void cameraState(PersonStatus status, float height, float distance) {
    portENTER_CRITICAL(&s_state_mux);
    updateCameraLocked(status, height, distance);
    portEXIT_CRITICAL(&s_state_mux);
}

bool cameraStateForScaleSession(uint32_t session, PersonStatus status, float height,
                                float distance, MeasurementView& out) {
    portENTER_CRITICAL(&s_state_mux);
    const auto scale = s_scale_presence.snapshot(clockMs());
    const bool valid = scale.present() && scale.sessionId == session;
    if (valid) {
        updateCameraLocked(status, height, distance);
        out = s_measurements.view(clockMs());
    }
    portEXIT_CRITICAL(&s_state_mux);
    return valid;
}

void clearMeasurementHold() {
    portENTER_CRITICAL(&s_state_mux);
    s_measurements.clearHold();
    portEXIT_CRITICAL(&s_state_mux);
}

MeasurementView stateSnapshot() {
    portENTER_CRITICAL(&s_state_mux);
    MeasurementView view = s_measurements.view(clockMs());
    portEXIT_CRITICAL(&s_state_mux);
    return view;
}

// =========================================================================
// 3. Configuration, MAC Filter & NVS Management
// =========================================================================
static EdgeConfig s_config;
static SemaphoreHandle_t s_config_mutex = nullptr;
static SemaphoreHandle_t s_usb_tx_mutex = nullptr;
static bool s_in_config_session = false;
static bool s_staging_valid = true;
static bool s_wifi_config_invalid = false;
static EdgeConfig s_staging_config;
static std::atomic<UsbLineHandler> s_usb_line_handler{nullptr};

static uint8_t s_scale_mac_le[6] = {};
static bool s_scale_mac_enabled = false;
static portMUX_TYPE s_ble_mac_mux = portMUX_INITIALIZER_UNLOCKED;

static int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static bool parseMacLe(const char* text, uint8_t outLe[6]) {
    if (!text || strlen(text) != 17) return false;
    for (int i = 0; i < 6; ++i) {
        const char* p = text + i * 3;
        const int hi = hexNibble(p[0]);
        const int lo = hexNibble(p[1]);
        if (hi < 0 || lo < 0) return false;
        if (i < 5 && p[2] != ':') return false;
        outLe[5 - i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

static void normalizeMacLower(char* mac) {
    if (!mac) return;
    for (size_t i = 0; mac[i]; ++i) {
        mac[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(mac[i])));
    }
}

static void updateScaleMacFilter(const char* macStr) {
    uint8_t parsed[6] = {};
    bool enable = false;
    if (macStr && macStr[0] != '\0' && strcmp(macStr, "00:00:00:00:00:00") != 0) {
        if (parseMacLe(macStr, parsed)) {
            enable = true;
        } else {
            ESP_LOGW(TAG, "Ignoring invalid scale MAC filter '%s'", macStr);
        }
    }
    portENTER_CRITICAL(&s_ble_mac_mux);
    s_scale_mac_enabled = enable;
    if (enable) memcpy(s_scale_mac_le, parsed, sizeof(s_scale_mac_le));
    portEXIT_CRITICAL(&s_ble_mac_mux);
}

static bool parseFloatStrict(const std::string& val, float minVal, float maxVal, float& out, bool allowZero = false) {
    if (val.empty()) return false;
    for (unsigned char c : val) {
        if (std::isspace(c)) return false;
    }
    char* end = nullptr;
    const float v = strtof(val.c_str(), &end);
    if (end == val.c_str() || *end != '\0' || !std::isfinite(v)) return false;
    if ((v >= minVal && v <= maxVal) || (allowZero && v == 0.0f)) {
        out = v;
        return true;
    }
    return false;
}

static bool parseUint16Strict(const std::string& val, uint16_t minVal, uint16_t maxVal, uint16_t& out) {
    if (val.empty()) return false;
    uint32_t acc = 0;
    for (unsigned char c : val) {
        if (c < '0' || c > '9') return false;
        acc = acc * 10U + static_cast<uint32_t>(c - '0');
        if (acc > maxVal) return false;
    }
    if (acc < minVal) return false;
    out = static_cast<uint16_t>(acc);
    return true;
}

static bool parseIntStrict(const std::string& val, int minVal, int maxVal, int& out) {
    if (val.empty()) return false;
    size_t idx = 0;
    bool neg = false;
    if (val[0] == '-') {
        neg = true;
        idx = 1;
    }
    if (idx >= val.size()) return false;
    int64_t acc = 0;
    for (; idx < val.size(); ++idx) {
        const unsigned char c = static_cast<unsigned char>(val[idx]);
        if (c < '0' || c > '9') return false;
        acc = acc * 10 + static_cast<int64_t>(c - '0');
        if (!neg && acc > maxVal) return false;
        if (neg && -acc < minVal) return false;
    }
    const int64_t signedVal = neg ? -acc : acc;
    if (signedVal < minVal || signedVal > maxVal) return false;
    out = static_cast<int>(signedVal);
    return true;
}

static void normalizeCameraUrl(char* url, size_t cap) {
    if (!url || !url[0]) return;
    size_t len = strlen(url);
    while (len > 0 && url[len - 1] == '/') {
        url[--len] = '\0';
    }
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        char tmp[160];
        snprintf(tmp, sizeof(tmp), "http://%s", url);
        strncpy(url, tmp, cap - 1);
        url[cap - 1] = '\0';
        len = strlen(url);
    }
    const char* host = (strncmp(url, "https://", 8) == 0) ? (url + 8) : (url + 7);
    if (strchr(host, '/') == nullptr && len + 8 < cap) {
        strcat(url, "/capture");
    }
}

static void sanitizeConfig(EdgeConfig& cfg) {
    if (!std::isfinite(cfg.distanceM) || cfg.distanceM < 0.1f || cfg.distanceM > 20.0f) {
        cfg.distanceM = 3.0f;
    }
    if (!std::isfinite(cfg.referenceCm) || cfg.referenceCm < 30.0f || cfg.referenceCm > 250.0f) {
        cfg.referenceCm = 183.0f;
    }
    if (!std::isfinite(cfg.focalPx) || (cfg.focalPx != 0.0f && (cfg.focalPx < 0.01f || cfg.focalPx > 20.0f))) {
        cfg.focalPx = 0.0f;
    }
    if (!std::isfinite(cfg.cameraHeightCm) || cfg.cameraHeightCm < 0.0f || cfg.cameraHeightCm > 400.0f) {
        cfg.cameraHeightCm = 0.0f;
    }
    if (!std::isfinite(cfg.tiltDeg) || cfg.tiltDeg < -79.0f || cfg.tiltDeg > 79.0f) {
        cfg.tiltDeg = 0.0f;
    }
    if (cfg.holdSec > 60) {
        cfg.holdSec = 10;
    }
    if (cfg.servoPin != -1 && !isValidServoPin(cfg.servoPin)) {
        cfg.servoPin = kServoDefaultPin;
    }
    if (!std::isfinite(cfg.servoMinDeg) || !std::isfinite(cfg.servoMaxDeg) ||
        cfg.servoMinDeg < kServoHardMinDeg || cfg.servoMaxDeg > kServoHardMaxDeg ||
        cfg.servoMinDeg >= cfg.servoMaxDeg) {
        cfg.servoMinDeg = kServoDefaultMinDeg;
        cfg.servoMaxDeg = kServoDefaultMaxDeg;
    }
    cfg.servoBottomDeg = clampServoDeg(
        std::isfinite(cfg.servoBottomDeg) ? cfg.servoBottomDeg : kServoDefaultBottomDeg,
        cfg.servoMinDeg, cfg.servoMaxDeg);
    cfg.servoTopDeg = clampServoDeg(
        std::isfinite(cfg.servoTopDeg) ? cfg.servoTopDeg : kServoDefaultTopDeg,
        cfg.servoMinDeg, cfg.servoMaxDeg);
    if (!std::isfinite(cfg.servoStepDeg) || cfg.servoStepDeg < 1.0f || cfg.servoStepDeg > 30.0f) {
        cfg.servoStepDeg = kServoDefaultStepDeg;
    }
    if (!std::isfinite(cfg.servoLevelDeg) ||
        cfg.servoLevelDeg < kServoHardMinDeg || cfg.servoLevelDeg > kServoHardMaxDeg) {
        cfg.servoLevelDeg = kServoDefaultLevelDeg;
    }
    if (cfg.servoDir != -1 && cfg.servoDir != 1) {
        cfg.servoDir = kServoDefaultDir;
    }
    if (cfg.servoSettleMs < 50 || cfg.servoSettleMs > 2000) {
        cfg.servoSettleMs = kServoDefaultSettleMs;
    }
    normalizeMacLower(cfg.scaleMac);
    uint8_t dummy[6];
    if (cfg.scaleMac[0] != '\0' && !parseMacLe(cfg.scaleMac, dummy)) {
        cfg.scaleMac[0] = '\0';
    }
}

static void loadConfigFromNvs(EdgeConfig& cfg) {
    nvs_handle_t h;
    if (nvs_open("edge_cfg", NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof(cfg.ssid);
        nvs_get_str(h, "ssid", cfg.ssid, &sz);
        sz = sizeof(cfg.password);
        nvs_get_str(h, "pass", cfg.password, &sz);
        sz = sizeof(cfg.cameraUrl);
        nvs_get_str(h, "camera", cfg.cameraUrl, &sz);
        normalizeCameraUrl(cfg.cameraUrl, sizeof(cfg.cameraUrl));
        sz = sizeof(cfg.scaleMac);
        nvs_get_str(h, "mac", cfg.scaleMac, &sz);

        uint8_t gm = 0;
        if (nvs_get_u8(h, "gmode", &gm) == ESP_OK) cfg.groundMode = (gm != 0);

        uint16_t holdSec = cfg.holdSec;
        if (nvs_get_u16(h, "hold_sec", &holdSec) == ESP_OK) cfg.holdSec = holdSec;

        int8_t srvPin = cfg.servoPin;
        if (nvs_get_i8(h, "srv_pin", &srvPin) == ESP_OK) cfg.servoPin = srvPin;
        int8_t srvDir = cfg.servoDir;
        if (nvs_get_i8(h, "srv_dir", &srvDir) == ESP_OK) cfg.servoDir = srvDir;
        uint16_t srvStl = cfg.servoSettleMs;
        if (nvs_get_u16(h, "srv_stl", &srvStl) == ESP_OK) cfg.servoSettleMs = srvStl;

        sz = sizeof(float);
        nvs_get_blob(h, "dist", &cfg.distanceM, &sz);
        sz = sizeof(float);
        nvs_get_blob(h, "ref_cm", &cfg.referenceCm, &sz);
        sz = sizeof(float);
        nvs_get_blob(h, "focal", &cfg.focalPx, &sz);
        sz = sizeof(float);
        nvs_get_blob(h, "cam_h", &cfg.cameraHeightCm, &sz);
        sz = sizeof(float);
        nvs_get_blob(h, "tilt", &cfg.tiltDeg, &sz);
        sz = sizeof(float);
        nvs_get_blob(h, "srv_min", &cfg.servoMinDeg, &sz);
        sz = sizeof(float);
        nvs_get_blob(h, "srv_max", &cfg.servoMaxDeg, &sz);
        sz = sizeof(float);
        nvs_get_blob(h, "srv_bot", &cfg.servoBottomDeg, &sz);
        sz = sizeof(float);
        nvs_get_blob(h, "srv_top", &cfg.servoTopDeg, &sz);
        sz = sizeof(float);
        nvs_get_blob(h, "srv_step", &cfg.servoStepDeg, &sz);
        sz = sizeof(float);
        nvs_get_blob(h, "srv_lvl", &cfg.servoLevelDeg, &sz);

        nvs_close(h);
    }

    sanitizeConfig(cfg);
    updateScaleMacFilter(cfg.scaleMac);
    applyHoldSec(cfg.holdSec);
    ESP_LOGI(TAG, "Configuration loaded: camera=%s mode=%s mac=%s hold=%us servo_pin=%d",
             cfg.cameraUrl, cfg.groundMode ? "ground" : "fixed",
             cfg.scaleMac[0] ? cfg.scaleMac : "(any)",
             static_cast<unsigned>(cfg.holdSec),
             static_cast<int>(cfg.servoPin));
}

static void saveConfigToNvs(const EdgeConfig& cfg) {
    nvs_handle_t h;
    if (nvs_open("edge_cfg", NVS_READWRITE, &h) != ESP_OK) return;

    nvs_set_str(h, "ssid", cfg.ssid);
    nvs_set_str(h, "pass", cfg.password);
    nvs_set_str(h, "camera", cfg.cameraUrl);
    nvs_set_str(h, "mac", cfg.scaleMac);
    nvs_set_u8(h, "gmode", cfg.groundMode ? 1 : 0);
    nvs_set_u16(h, "hold_sec", cfg.holdSec);
    nvs_set_i8(h, "srv_pin", cfg.servoPin);
    nvs_set_i8(h, "srv_dir", cfg.servoDir);
    nvs_set_u16(h, "srv_stl", cfg.servoSettleMs);
    nvs_set_blob(h, "dist", &cfg.distanceM, sizeof(float));
    nvs_set_blob(h, "ref_cm", &cfg.referenceCm, sizeof(float));
    nvs_set_blob(h, "focal", &cfg.focalPx, sizeof(float));
    nvs_set_blob(h, "cam_h", &cfg.cameraHeightCm, sizeof(float));
    nvs_set_blob(h, "tilt", &cfg.tiltDeg, sizeof(float));
    nvs_set_blob(h, "srv_min", &cfg.servoMinDeg, sizeof(float));
    nvs_set_blob(h, "srv_max", &cfg.servoMaxDeg, sizeof(float));
    nvs_set_blob(h, "srv_bot", &cfg.servoBottomDeg, sizeof(float));
    nvs_set_blob(h, "srv_top", &cfg.servoTopDeg, sizeof(float));
    nvs_set_blob(h, "srv_step", &cfg.servoStepDeg, sizeof(float));
    nvs_set_blob(h, "srv_lvl", &cfg.servoLevelDeg, sizeof(float));

    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Configuration committed to NVS");
}

EdgeConfig configSnapshot() {
    EdgeConfig copy;
    xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    copy = s_config;
    xSemaphoreGive(s_config_mutex);
    return copy;
}

void saveFocalIfUnchanged(const EdgeConfig& original, float focal) {
    if (!std::isfinite(focal) || focal < 0.01f || focal > 20.0f) {
        ESP_LOGW(TAG, "Rejected invalid auto-calibrated focalPx: %.4f", focal);
        return;
    }
    xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    if (s_config.revision == original.revision &&
        std::fabs(s_config.focalPx - original.focalPx) < 0.0001f) {
        s_config.focalPx = focal;
        nvs_handle_t h;
        if (nvs_open("edge_cfg", NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_blob(h, "focal", &focal, sizeof(focal));
            nvs_commit(h);
            nvs_close(h);
        }
        ESP_LOGI(TAG, "Saved auto-calibrated focalPx: %.4f", focal);
    }
    xSemaphoreGive(s_config_mutex);
}

// =========================================================================
// 4. Wi-Fi Station (Serialized on Default Event Loop)
// =========================================================================
static std::atomic<bool> s_wifi_ready{false};
static std::atomic<bool> s_wifi_started{false};
static char s_wifi_ip[16] = "no IP";
static portMUX_TYPE s_ip_mux = portMUX_INITIALIZER_UNLOCKED;
ESP_EVENT_DEFINE_BASE(EDGE_WIFI_CONTROL);
enum { WIFI_CONTROL_CONFIG, WIFI_CONTROL_RETRY };
static esp_timer_handle_t s_wifi_retry_timer = nullptr;
static WifiReconnect s_wifi_reconnect;
static wifi_config_t s_wifi_desired_config = {};
static bool s_wifi_waiting_stop = false;

static void wifiClearReady() {
    s_wifi_ready.store(false);
    portENTER_CRITICAL(&s_ip_mux);
    strcpy(s_wifi_ip, "no IP");
    portEXIT_CRITICAL(&s_ip_mux);
}

static void wifiCancelRetry() {
    s_wifi_reconnect.cancel();
    esp_timer_stop(s_wifi_retry_timer); // An inactive timer is harmless.
}

static void wifiRetryTimer(void*) {
    // Never connect or wait on the system timer task. A stale wakeup is checked
    // against the current policy/deadline when the event loop receives it.
    if (esp_event_post(EDGE_WIFI_CONTROL, WIFI_CONTROL_RETRY, nullptr, 0, 0) != ESP_OK) {
        // Queue full: preserve recovery without blocking this timer callback.
        // INVALID_STATE means a newer retry has already armed this timer.
        esp_timer_start_once(s_wifi_retry_timer, 100000);
    }
}

static void wifiArmRetry() {
    if (!s_wifi_reconnect.pending()) return;
    esp_timer_stop(s_wifi_retry_timer);
    const uint32_t wait = std::max<uint32_t>(1, s_wifi_reconnect.remaining(clockMs()));
    const esp_err_t rc = esp_timer_start_once(s_wifi_retry_timer, uint64_t(wait) * 1000);
    // A queue-full timer callback can rearm concurrently. Its earlier wakeup
    // is safe: the event loop checks the deadline again before connecting.
    ESP_ERROR_CHECK(rc == ESP_ERR_INVALID_STATE ? ESP_OK : rc);
}

static void wifiScheduleRetry() {
    s_wifi_reconnect.disconnected(clockMs());
    wifiArmRetry();
}

static void wifiConnectNow() {
    if (!s_wifi_reconnect.enabled() || s_wifi_waiting_stop || !s_wifi_started.load()) return;
    wifiCancelRetry();
    const esp_err_t rc = esp_wifi_connect();
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi connect failed: %s; retry in 2 seconds", esp_err_to_name(rc));
        wifiScheduleRetry();
    }
}

static void applyWifiPowerSave(bool active) {
    // Keep the station associated; MIN_MODEM wakes at every DTIM1 beacon so the
    // first HTTP capture after stepping on the scale starts without a beacon stall.
    ESP_ERROR_CHECK(esp_wifi_set_ps(active ? WIFI_PS_NONE : WIFI_PS_MIN_MODEM));
}

static void wifiStartDesiredConfig() {
    if (!s_wifi_reconnect.enabled()) return;
    esp_err_t rc = esp_wifi_set_config(WIFI_IF_STA, &s_wifi_desired_config);
    if (rc == ESP_OK) {
        applyWifiPowerSave(s_power_active.load());
        rc = esp_wifi_start();
    }
    s_wifi_started.store(rc == ESP_OK);
    if (rc != ESP_OK) ESP_LOGE(TAG, "Wi-Fi start failed: %s", esp_err_to_name(rc));
}

static void wifiEventHandler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data) {
    if (event_base == EDGE_WIFI_CONTROL && event_id == WIFI_CONTROL_CONFIG) {
        const auto& cfg = *static_cast<const EdgeConfig*>(event_data);
        WifiCredentials credentials;
        const char* error = prepareWifiCredentials(
            std::string_view(cfg.ssid, strnlen(cfg.ssid, sizeof(cfg.ssid))),
            std::string_view(cfg.password, strnlen(cfg.password, sizeof(cfg.password))), credentials);
        if (error) {
            ESP_LOGE(TAG, "Wi-Fi configuration rejected: %s", error);
            return; // Preserve the current connection for an invalid request.
        }
        wifiCancelRetry();
        wifiClearReady();
        s_wifi_reconnect.enable(cfg.ssid[0] != '\0');
        if (!s_wifi_reconnect.enabled()) {
            ESP_LOGW(TAG, "No Wi-Fi SSID configured; waiting for USB configuration");
        }
        s_wifi_desired_config = {};
        static_assert(sizeof(s_wifi_desired_config.sta.ssid) == credentials.ssid.size());
        static_assert(sizeof(s_wifi_desired_config.sta.password) == credentials.password.size());
        memcpy(s_wifi_desired_config.sta.ssid, credentials.ssid.data(), credentials.ssid.size());
        memcpy(s_wifi_desired_config.sta.password, credentials.password.data(), credentials.password.size());
        s_wifi_desired_config.sta.threshold.authmode = credentials.open ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_WPA2_PSK;
        if (s_wifi_waiting_stop) return; // Keep the newest config until STOP.
        if (s_wifi_started.load()) {
            // STOP is an event-ordering fence: old disconnect/start events are
            // ignored until it arrives, then only the newest config is started.
            s_wifi_waiting_stop = true;
            const esp_err_t rc = esp_wifi_stop();
            if (rc != ESP_OK) {
                s_wifi_waiting_stop = false;
                s_wifi_reconnect.enable(false);
                ESP_LOGE(TAG, "Wi-Fi stop for configuration failed: %s", esp_err_to_name(rc));
            }
        } else {
            wifiStartDesiredConfig();
        }
    } else if (event_base == EDGE_WIFI_CONTROL && event_id == WIFI_CONTROL_RETRY) {
        if (s_wifi_reconnect.due(clockMs())) wifiConnectNow();
        else wifiArmRetry(); // An old timer event must not connect early.
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_STOP) {
        s_wifi_started.store(false);
        wifiCancelRetry();
        wifiClearReady();
        if (s_wifi_waiting_stop) {
            s_wifi_waiting_stop = false;
            wifiStartDesiredConfig();
        } else {
            s_wifi_reconnect.enable(false);
        }
    } else if (s_wifi_waiting_stop || !s_wifi_reconnect.enabled()) {
        return;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        wifiConnectNow();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        wifiCancelRetry(); // Associated: wait for DHCP, do not start another connect.
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifiClearReady();
        ESP_LOGW(TAG, "Wi-Fi disconnected; retrying in 2 seconds...");
        wifiScheduleRetry();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifiCancelRetry();
        auto* event = reinterpret_cast<ip_event_got_ip_t*>(event_data);
        ESP_LOGI(TAG, "Wi-Fi connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&event->ip_info.ip));
        portENTER_CRITICAL(&s_ip_mux);
        strcpy(s_wifi_ip, ip);
        portEXIT_CRITICAL(&s_ip_mux);
        s_wifi_ready.store(true);
        s_wifi_connect_epoch.fetch_add(1);
    }
}

void wifiIp(char* out, size_t size) {
    if (!out || !size) return;
    portENTER_CRITICAL(&s_ip_mux);
    snprintf(out, size, "%s", s_wifi_ip);
    portEXIT_CRITICAL(&s_ip_mux);
}

static void applyWifiConfig(const EdgeConfig& cfg) {
    // Called by init/USB task, never from the event loop. The event copies cfg;
    // all connect/config/timer policy changes are serialized on that loop.
    ESP_ERROR_CHECK(esp_event_post(EDGE_WIFI_CONTROL, WIFI_CONTROL_CONFIG,
                                   &cfg, sizeof(cfg), portMAX_DELAY));
}

bool wifiReady() {
    return s_wifi_ready.load();
}

int wifiRssi() {
    if (!wifiReady()) return 0;
    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.rssi;
    }
    return 0;
}

// =========================================================================
// 5. USB Framing & Communication (SSV1 Protocol)
// =========================================================================
#pragma pack(push, 1)
struct UsbPacketHeader {
    char magic[4];       // 'S', 'S', 'V', '1'
    uint32_t json_len;
    uint32_t jpeg_len;
    uint32_t crc;
};
#pragma pack(pop)

static bool usbWriteAll(const void* data, size_t length, uint32_t timeout_ms) {
    const auto* ptr = static_cast<const uint8_t*>(data);
    const uint32_t deadline = clockMs() + timeout_ms;
    size_t sent = 0;
    while (sent < length) {
        // Chunk size must be <= usb_cfg.tx_buffer_size because ESP-IDF's
        // xRingbufferSend immediately rejects any write larger than the ringbuffer.
        const size_t chunk = std::min<size_t>(length - sent, 512);
        const int32_t remain = static_cast<int32_t>(deadline - clockMs());
        if (remain <= 0) return false;
        // When a PC USB viewer is open, Windows drains 512 B in < 2 ms.
        // If the ringbuffer stays full for 25 ms, no viewer is reading: abort immediately
        // so the main loop never stalls when the USB viewer is closed.
        const TickType_t wait_ticks = std::max<TickType_t>(1, pdMS_TO_TICKS(std::min<int32_t>(remain, 25)));
        const int n = usb_serial_jtag_write_bytes(ptr + sent, chunk, wait_ticks);
        if (n > 0) {
            sent += static_cast<size_t>(n);
        } else {
            return false;
        }
    }
    return true;
}

void usbFrame(const uint8_t* jpeg, size_t length, const char* metadata) {
    if (!metadata || !usb_serial_jtag_is_connected()) return;
    const uint32_t json_len = static_cast<uint32_t>(strlen(metadata));
    const uint32_t jpeg_len = (jpeg && length > 0) ? static_cast<uint32_t>(length) : 0;

    uint32_t crc = esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(metadata), json_len);
    if (jpeg_len > 0) {
        crc = esp_rom_crc32_le(crc, jpeg, jpeg_len);
    }

    UsbPacketHeader hdr;
    memcpy(hdr.magic, "SSV1", 4);
    hdr.json_len = json_len;
    hdr.jpeg_len = jpeg_len;
    hdr.crc = crc;

    // Keep config replies and image/telemetry packets from interleaving.
    if (!s_usb_tx_mutex || xSemaphoreTake(s_usb_tx_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    const bool headerSent = usbWriteAll(&hdr, sizeof(hdr), 50);
    const bool jsonSent = headerSent && usbWriteAll(metadata, json_len, 100);
    if (jsonSent && jpeg_len > 0) {
        usbWriteAll(jpeg, jpeg_len, 1500);
    }
    xSemaphoreGive(s_usb_tx_mutex);
}

void setUsbLineHandler(UsbLineHandler handler) {
    s_usb_line_handler.store(handler);
}

void usbWriteText(const char* text) {
    if (!text || !text[0] || !usb_serial_jtag_is_connected()) return;
    if (!s_usb_tx_mutex || xSemaphoreTake(s_usb_tx_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    usbWriteAll(text, strlen(text), 200);
    xSemaphoreGive(s_usb_tx_mutex);
}

static void processConfigLine(const std::string& line) {
    if (line == "BEGIN") {
        s_staging_config = configSnapshot();
        s_in_config_session = true;
        s_staging_valid = true;
        s_wifi_config_invalid = false;
    } else if (s_in_config_session && line.rfind("CFG ", 0) == 0) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            s_staging_valid = false;
            return;
        }
        const std::string key = line.substr(4, eq - 4);
        const std::string val = line.substr(eq + 1);
        if (key == "ssid") {
            if (val.size() > 32 || val.find('\0') != std::string::npos) {
                s_wifi_config_invalid = true;
                s_staging_valid = false;
            } else {
                strncpy(s_staging_config.ssid, val.c_str(), sizeof(s_staging_config.ssid) - 1);
                s_staging_config.ssid[sizeof(s_staging_config.ssid) - 1] = '\0';
            }
        } else if (key == "password") {
            if (val.size() > 64 || val.find('\0') != std::string::npos) {
                s_wifi_config_invalid = true;
                s_staging_valid = false;
            } else {
                strncpy(s_staging_config.password, val.c_str(), sizeof(s_staging_config.password) - 1);
                s_staging_config.password[sizeof(s_staging_config.password) - 1] = '\0';
            }
        } else if (key == "camera") {
            if (val.empty() || val.size() >= sizeof(s_staging_config.cameraUrl)) {
                s_staging_valid = false;
            } else {
                strncpy(s_staging_config.cameraUrl, val.c_str(), sizeof(s_staging_config.cameraUrl) - 1);
                s_staging_config.cameraUrl[sizeof(s_staging_config.cameraUrl) - 1] = '\0';
                normalizeCameraUrl(s_staging_config.cameraUrl, sizeof(s_staging_config.cameraUrl));
            }
        } else if (key == "mac") {
            uint8_t dummy[6];
            if (val.empty() || val == "00:00:00:00:00:00") {
                s_staging_config.scaleMac[0] = '\0';
            } else if (parseMacLe(val.c_str(), dummy)) {
                strncpy(s_staging_config.scaleMac, val.c_str(), sizeof(s_staging_config.scaleMac) - 1);
                s_staging_config.scaleMac[sizeof(s_staging_config.scaleMac) - 1] = '\0';
                normalizeMacLower(s_staging_config.scaleMac);
            } else {
                ESP_LOGW(TAG, "Rejected invalid MAC in config: '%s'", val.c_str());
                s_staging_valid = false;
            }
        } else if (key == "mode") {
            if (val == "ground") s_staging_config.groundMode = true;
            else if (val == "fixed") s_staging_config.groundMode = false;
            else s_staging_valid = false;
        } else if (key == "distance") {
            if (!parseFloatStrict(val, 0.1f, 20.0f, s_staging_config.distanceM)) s_staging_valid = false;
        } else if (key == "reference") {
            if (!parseFloatStrict(val, 30.0f, 250.0f, s_staging_config.referenceCm)) s_staging_valid = false;
        } else if (key == "focal") {
            if (!parseFloatStrict(val, 0.01f, 20.0f, s_staging_config.focalPx, true)) s_staging_valid = false;
        } else if (key == "height") {
            if (!parseFloatStrict(val, 0.0f, 400.0f, s_staging_config.cameraHeightCm, true)) s_staging_valid = false;
        } else if (key == "tilt") {
            if (!parseFloatStrict(val, -79.0f, 79.0f, s_staging_config.tiltDeg)) s_staging_valid = false;
        } else if (key == "hold_sec") {
            if (!parseUint16Strict(val, 0, 60, s_staging_config.holdSec)) s_staging_valid = false;
        } else if (key == "servo_pin") {
            int pin = 0;
            if (!parseIntStrict(val, -1, 48, pin) || (pin != -1 && !isValidServoPin(pin))) {
                s_staging_valid = false;
            } else {
                s_staging_config.servoPin = static_cast<int8_t>(pin);
            }
        } else if (key == "servo_min") {
            if (!parseFloatStrict(val, kServoHardMinDeg, kServoHardMaxDeg, s_staging_config.servoMinDeg)) s_staging_valid = false;
        } else if (key == "servo_max") {
            if (!parseFloatStrict(val, kServoHardMinDeg, kServoHardMaxDeg, s_staging_config.servoMaxDeg)) s_staging_valid = false;
        } else if (key == "servo_bottom") {
            if (!parseFloatStrict(val, kServoHardMinDeg, kServoHardMaxDeg, s_staging_config.servoBottomDeg)) s_staging_valid = false;
        } else if (key == "servo_top") {
            if (!parseFloatStrict(val, kServoHardMinDeg, kServoHardMaxDeg, s_staging_config.servoTopDeg)) s_staging_valid = false;
        } else if (key == "servo_step") {
            if (!parseFloatStrict(val, 1.0f, 30.0f, s_staging_config.servoStepDeg)) s_staging_valid = false;
        } else if (key == "servo_level") {
            if (!parseFloatStrict(val, kServoHardMinDeg, kServoHardMaxDeg, s_staging_config.servoLevelDeg)) s_staging_valid = false;
        } else if (key == "servo_dir") {
            int dir = 0;
            if (!parseIntStrict(val, -1, 1, dir) || (dir != -1 && dir != 1)) {
                s_staging_valid = false;
            } else {
                s_staging_config.servoDir = static_cast<int8_t>(dir);
            }
        } else if (key == "servo_settle") {
            if (!parseUint16Strict(val, 50, 2000, s_staging_config.servoSettleMs)) s_staging_valid = false;
        } else {
            ESP_LOGW(TAG, "Unknown configuration key '%s'", key.c_str());
            s_staging_valid = false;
        }
    } else if (s_in_config_session && line == "APPLY") {
        s_in_config_session = false;
        WifiCredentials credentials;
        const char* wifi_error = prepareWifiCredentials(
            std::string_view(s_staging_config.ssid, strnlen(s_staging_config.ssid, sizeof(s_staging_config.ssid))),
            std::string_view(s_staging_config.password, strnlen(s_staging_config.password, sizeof(s_staging_config.password))),
            credentials);
        const bool servoRangeValid =
            s_staging_config.servoMinDeg < s_staging_config.servoMaxDeg &&
            s_staging_config.servoBottomDeg >= s_staging_config.servoMinDeg &&
            s_staging_config.servoBottomDeg <= s_staging_config.servoMaxDeg &&
            s_staging_config.servoTopDeg >= s_staging_config.servoMinDeg &&
            s_staging_config.servoTopDeg <= s_staging_config.servoMaxDeg;
        if (!s_staging_valid || s_wifi_config_invalid || wifi_error || !servoRangeValid) {
            ESP_LOGW(TAG, "Rejected invalid configuration session (%s)",
                     wifi_error ? wifi_error : (servoRangeValid ? "invalid parameter" : "invalid servo bounds"));
            usbFrame(nullptr, 0, "{\"kind\":\"config\",\"ok\":false}");
            return;
        }

        xSemaphoreTake(s_config_mutex, portMAX_DELAY);
        const bool wifi_changed = (strcmp(s_config.ssid, s_staging_config.ssid) != 0 ||
                                  strcmp(s_config.password, s_staging_config.password) != 0);
        const bool needs_wifi_start = (!s_wifi_started.load() && s_staging_config.ssid[0] != '\0');
        s_staging_config.revision = s_config.revision + 1;
        s_config = s_staging_config;
        saveConfigToNvs(s_config);
        updateScaleMacFilter(s_config.scaleMac);
        const uint16_t appliedHoldSec = s_config.holdSec;
        xSemaphoreGive(s_config_mutex);

        applyHoldSec(appliedHoldSec);
        clearMeasurementHold();
        edgeNotifyControl();
        if (wifi_changed || needs_wifi_start) {
            applyWifiConfig(s_staging_config);
        }
        usbFrame(nullptr, 0, "{\"kind\":\"config\",\"ok\":true}");
    } else if (!s_in_config_session) {
        const auto handler = s_usb_line_handler.load();
        if (handler) {
            handler(line.c_str());
        }
    }
}

static void usbReceiverTask(void* pvParameters) {
    uint8_t buf[256];
    std::string line_accum;
    line_accum.reserve(260);
    while (true) {
        const int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), portMAX_DELAY);
        for (int i = 0; i < n; ++i) {
            const char c = static_cast<char>(buf[i]);
            if (c == '\n' || c == '\r') {
                if (!line_accum.empty()) {
                    processConfigLine(line_accum);
                    line_accum.clear();
                }
            } else {
                if (line_accum.size() < 256) line_accum.push_back(c);
            }
        }
    }
}

// =========================================================================
// 6. LCD 16x2 (PCF8574 over I2C)
// =========================================================================
static uint8_t s_lcd_addr = 0x21; // Default lab address
static bool s_lcd_ready = false;
static bool s_lcd_backlight = true; // Protected by s_lcd_mutex after init.
static bool s_lcd_holding = false;
static MeasurementView s_lcd_result;
static char s_lcd_msg_first[17] = {};
static char s_lcd_msg_second[17] = {};
static char s_lcd_prev[2][17] = {};
static bool s_lcd_prev_valid[2] = {false, false};
static uint32_t s_lcd_override_until = 0;
static bool s_lcd_override_active = false;
static SemaphoreHandle_t s_lcd_mutex = nullptr;

#if defined(USE_I2C_MASTER_NEW)
static i2c_master_bus_handle_t s_i2c_bus = nullptr;
static i2c_master_dev_handle_t s_i2c_dev = nullptr;

static bool i2cTransmit(const uint8_t* data, size_t len) {
    if (!s_i2c_dev) return false;
    return i2c_master_transmit(s_i2c_dev, data, len, 50) == ESP_OK;
}
#elif defined(USE_I2C_MASTER_LEGACY)
static bool i2cTransmit(const uint8_t* data, size_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_lcd_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, const_cast<uint8_t*>(data), len, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return ret == ESP_OK;
}
#else
static bool i2cTransmit(const uint8_t* data, size_t len) { return false; }
#endif

static void pcfExpanderWrite(uint8_t val) {
    const uint8_t byte = static_cast<uint8_t>(val | (s_lcd_backlight ? 0x08 : 0x00));
    i2cTransmit(&byte, 1);
}

static void pcfWriteNibble(uint8_t nibble, bool is_data) {
    const uint8_t b = (nibble & 0xF0) | (is_data ? 0x01 : 0x00);
    pcfExpanderWrite(b);                               // Setup RS/RW + Data before E rising edge
    esp_rom_delay_us(2);
    pcfExpanderWrite(static_cast<uint8_t>(b | 0x04));  // Enable HIGH
    esp_rom_delay_us(2);                               // Enable pulse width > 450 ns
    pcfExpanderWrite(static_cast<uint8_t>(b & ~0x04)); // Enable LOW (latch)
    esp_rom_delay_us(60);                              // HD44780 command/data settle > 37 us
}

static void lcdWriteByte(uint8_t val, bool is_data) {
    pcfWriteNibble(val & 0xF0, is_data);
    pcfWriteNibble((val << 4) & 0xF0, is_data);
    if (!is_data && val <= 0x03) {
        esp_rom_delay_us(2000); // Clear Display / Return Home require > 1.52 ms
    } else {
        esp_rom_delay_us(60);
    }
}

// Re-synchronizes the HD44780 4-bit nibble boundary without clearing DDRAM, so
// any single dropped nibble from wire noise or power transient self-heals
// back to byte alignment without flickering the screen.
static void lcdResync4Bit() {
    pcfExpanderWrite(0x00);
    vTaskDelay(pdMS_TO_TICKS(2));
    pcfWriteNibble(0x30, false);
    vTaskDelay(pdMS_TO_TICKS(6));
    pcfWriteNibble(0x30, false);
    vTaskDelay(pdMS_TO_TICKS(6));
    pcfWriteNibble(0x30, false);
    esp_rom_delay_us(200);
    pcfWriteNibble(0x20, false); // Enter 4-bit mode, aligned on high nibble
    esp_rom_delay_us(200);

    lcdWriteByte(0x28, false); // 2 lines, 5x8 font
    lcdWriteByte(0x0C, false); // Display ON, Cursor OFF, Blink OFF
    lcdWriteByte(0x06, false); // Entry mode increment, no shift
}

static void lcdSetLine(uint8_t row, const char* text) {
    if (row > 1) return;
    char padded[17];
    const size_t len = text ? strlen(text) : 0;
    for (size_t i = 0; i < 16; ++i) {
        padded[i] = (i < len) ? text[i] : ' ';
    }
    padded[16] = '\0';
    if (s_lcd_prev_valid[row] && memcmp(s_lcd_prev[row], padded, 16) == 0) {
        return;
    }
    lcdWriteByte(row == 0 ? 0x80 : 0xC0, false);
    for (size_t i = 0; i < 16; ++i) {
        lcdWriteByte(static_cast<uint8_t>(padded[i]), true);
    }
    memcpy(s_lcd_prev[row], padded, sizeof(padded));
    s_lcd_prev_valid[row] = true;
}

static void initLcdHardware() {
    if (s_lcd_ready) return;

#if defined(USE_I2C_MASTER_NEW)
    if (!s_i2c_bus) {
        i2c_master_bus_config_t bus_cfg = {};
        bus_cfg.i2c_port = I2C_NUM_0;
        bus_cfg.sda_io_num = static_cast<gpio_num_t>(11);
        bus_cfg.scl_io_num = static_cast<gpio_num_t>(12);
        bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_cfg.glitch_ignore_cnt = 7;
        bus_cfg.flags.enable_internal_pullup = true;
        if (i2c_new_master_bus(&bus_cfg, &s_i2c_bus) != ESP_OK) return;
    }

    // Scan PCF8574 (0x20..0x27) & PCF8574A (0x38..0x3F), checking common lab addresses first.
    static constexpr uint8_t candidates[] = {
        0x21, 0x27, 0x3F, 0x20, 0x22, 0x23, 0x24, 0x25, 0x26,
        0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E
    };
    for (uint8_t addr : candidates) {
        if (i2c_master_probe(s_i2c_bus, addr, 30) == ESP_OK) {
            i2c_device_config_t dev_cfg = {};
            dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
            dev_cfg.device_address = addr;
            dev_cfg.scl_speed_hz = 50000;
            if (i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_i2c_dev) == ESP_OK) {
                s_lcd_addr = addr;
                s_lcd_ready = true;
                ESP_LOGI(TAG, "LCD detected at I2C address 0x%02X", s_lcd_addr);
                break;
            }
        }
    }
#elif defined(USE_I2C_MASTER_LEGACY)
    static bool s_legacy_bus_installed = false;
    if (!s_legacy_bus_installed) {
        i2c_config_t conf = {};
        conf.mode = I2C_MODE_MASTER;
        conf.sda_io_num = 11;
        conf.scl_io_num = 12;
        conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
        conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
        conf.master.clk_speed = 50000;
        i2c_param_config(I2C_NUM_0, &conf);
        if (i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0) == ESP_OK) {
            s_legacy_bus_installed = true;
        }
    }
    if (s_legacy_bus_installed) {
        s_lcd_ready = true;
    }
#endif

    if (!s_lcd_ready) {
        ESP_LOGW(TAG, "LCD backpack not found on SDA=11 SCL=12");
        return;
    }

    s_lcd_prev_valid[0] = false;
    s_lcd_prev_valid[1] = false;

    // HD44780 4-bit initialization sequence with exact microsecond timings.
    vTaskDelay(pdMS_TO_TICKS(50));
    lcdResync4Bit();
    lcdWriteByte(0x01, false); // Clear display
    esp_rom_delay_us(2000);
}

void lcdMessage(const char* first, const char* second) {
    xSemaphoreTake(s_lcd_mutex, portMAX_DELAY);
    strncpy(s_lcd_msg_first, first ? first : "", sizeof(s_lcd_msg_first) - 1);
    s_lcd_msg_first[sizeof(s_lcd_msg_first) - 1] = '\0';
    strncpy(s_lcd_msg_second, second ? second : "", sizeof(s_lcd_msg_second) - 1);
    s_lcd_msg_second[sizeof(s_lcd_msg_second) - 1] = '\0';
    s_lcd_override_until = clockMs() + 2000;
    s_lcd_override_active = true;
    if (s_lcd_ready) {
        lcdSetLine(0, s_lcd_msg_first);
        lcdSetLine(1, s_lcd_msg_second);
    }
    xSemaphoreGive(s_lcd_mutex);
}

void lcdClearMessage() {
    xSemaphoreTake(s_lcd_mutex, portMAX_DELAY);
    s_lcd_override_active = false;
    s_lcd_override_until = 0;
    xSemaphoreGive(s_lcd_mutex);
    if (s_lcd_wake_sem) {
        xSemaphoreGive(s_lcd_wake_sem);
    }
}

void lcdSetBacklight(bool on) {
    xSemaphoreTake(s_lcd_mutex, portMAX_DELAY);
    const bool prev = s_lcd_backlight;
    s_lcd_backlight = on;
    s_lcd_backlight_state.store(on);
    if (on && !prev) {
        s_lcd_prev_valid[0] = false;
        s_lcd_prev_valid[1] = false;
    }
    if (s_lcd_ready) {
        // E=0: changing only the backlight does not latch an LCD command.
        const uint8_t value = on ? 0x08 : 0x00;
        i2cTransmit(&value, 1);
    }
    xSemaphoreGive(s_lcd_mutex);
    if (on && !prev && s_lcd_wake_sem) {
        xSemaphoreGive(s_lcd_wake_sem);
    }
}

void lcdHoldResult(const MeasurementView* result) {
    xSemaphoreTake(s_lcd_mutex, portMAX_DELAY);
    s_lcd_holding = result && result->hasBMI;
    if (s_lcd_holding) s_lcd_result = *result;
    s_lcd_override_until = 0;
    s_lcd_override_active = false;
    const bool wake = s_lcd_holding;
    xSemaphoreGive(s_lcd_mutex);
    if (wake && s_lcd_wake_sem) {
        xSemaphoreGive(s_lcd_wake_sem);
    }
}

static void lcdTask(void* pvParameters) {
    uint32_t last_lcd_probe = clockMs();
    uint32_t resync_counter = 0;
    while (true) {
        if (!s_lcd_backlight_state.load()) {
            resync_counter = 0;
            xSemaphoreTake(s_lcd_wake_sem, portMAX_DELAY);
            continue;
        }
        const uint32_t now = clockMs();

        if (!s_lcd_ready) {
            if (now - last_lcd_probe >= 5000) {
                last_lcd_probe = now;
                xSemaphoreTake(s_lcd_mutex, portMAX_DELAY);
                if (s_lcd_backlight) {
                    initLcdHardware();
                    if (s_lcd_ready && s_lcd_override_active &&
                        static_cast<int32_t>(s_lcd_override_until - clockMs()) > 0) {
                        lcdSetLine(0, s_lcd_msg_first);
                        lcdSetLine(1, s_lcd_msg_second);
                    }
                }
                xSemaphoreGive(s_lcd_mutex);
            }
            xSemaphoreTake(s_lcd_wake_sem, pdMS_TO_TICKS(250));
            continue;
        }

        xSemaphoreTake(s_lcd_mutex, portMAX_DELAY);
        if (!s_lcd_backlight) {
            resync_counter = 0;
            xSemaphoreGive(s_lcd_mutex);
            continue; // Avoid I2C writes while asleep.
        }
        // Every 3 seconds (12 * 250 ms), re-assert 4-bit nibble alignment and
        // force-refresh both lines so any I2C wire glitch self-heals cleanly.
        if (++resync_counter >= 12) {
            resync_counter = 0;
            lcdResync4Bit();
            s_lcd_prev_valid[0] = false;
            s_lcd_prev_valid[1] = false;
        }
#ifdef EDGE_SCALE_ONLY
        const auto weight = scaleSnapshot();
        if (weight.hasWeight) {
            char line[20];
            snprintf(line, sizeof(line), "W:%.2f kg", weight.weightKg);
            lcdSetLine(0, line);
            lcdSetLine(1, weight.weightStable ? "Stable: LOCK" : "Measuring...");
        } else {
            lcdSetLine(0, "Mi Scale 2 ready");
            lcdSetLine(1, "Step on scale");
        }
#else
        bool overrideDrawn = false;
        if (s_lcd_override_active) {
            if (static_cast<int32_t>(s_lcd_override_until - now) > 0) {
                lcdSetLine(0, s_lcd_msg_first);
                lcdSetLine(1, s_lcd_msg_second);
                overrideDrawn = true;
            } else {
                s_lcd_override_active = false;
            }
        }

        if (!overrideDrawn) {
            const auto view = s_lcd_holding ? s_lcd_result : stateSnapshot();
            const auto scale = scaleSnapshot();
            const uint32_t phase = (now / 2500) % 2;

            if (!s_lcd_holding && !powerModeActive()) {
                if (scale.hasWeight) {
                    char l0[20];
                    snprintf(l0, sizeof(l0), "W:%.2fkg %s", scale.weightKg, scale.weightStable ? "LOCK" : "...");
                    lcdSetLine(0, l0);
                    lcdSetLine(1, "Step on scale");
                } else {
                    lcdSetLine(0, "SmartScale Ready");
                    lcdSetLine(1, "Step on scale");
                }
            } else if (view.status == PersonStatus::None) {
                if (scale.hasWeight) {
                    char l0[20];
                    snprintf(l0, sizeof(l0), "W:%.2fkg %s", scale.weightKg, scale.weightStable ? "LOCK" : "...");
                    lcdSetLine(0, l0);
                    lcdSetLine(1, "Look at camera");
                } else {
                    lcdSetLine(0, "No people");
                    lcdSetLine(1, "detected");
                }
            } else if (view.status == PersonStatus::Multiple) {
                lcdSetLine(0, "Multiple people");
                lcdSetLine(1, "One at a time");
            } else if (phase == 0) {
                char l0[20], l1[20];
                if (view.hasHeight) snprintf(l0, sizeof(l0), "H:%.1fcm", view.heightCm);
                else snprintf(l0, sizeof(l0), "H:-- (calib)");

                if (view.hasWeight) {
                    snprintf(l1, sizeof(l1), "W:%.2fkg %s", view.weightKg, view.weightStable ? "LOCK" : "...");
                } else {
                    snprintf(l1, sizeof(l1), "W:-- wait scale");
                }
                lcdSetLine(0, l0);
                lcdSetLine(1, l1);
            } else if (view.status == PersonStatus::NeedsCalibration) {
                lcdSetLine(0, "Person detected");
                lcdSetLine(1, "Need calibration");
            } else if (view.status == PersonStatus::Partial) {
                lcdSetLine(0, "Person detected");
                lcdSetLine(1, "Full body needed");
            } else {
                char l0[20], l1[20];
                if (view.hasHeight) {
                    if (view.held || s_lcd_holding) snprintf(l0, sizeof(l0), "D:%.2fm HOLD", view.distanceM);
                    else snprintf(l0, sizeof(l0), "D:%.2fm", view.distanceM);
                } else {
                    snprintf(l0, sizeof(l0), "D:--");
                }

                if (view.hasBMI) snprintf(l1, sizeof(l1), "BMI:%.2f", view.bmi);
                else snprintf(l1, sizeof(l1), "BMI:-- wait lock");

                lcdSetLine(0, l0);
                lcdSetLine(1, l1);
            }
        }
#endif
        xSemaphoreGive(s_lcd_mutex);
        xSemaphoreTake(s_lcd_wake_sem, pdMS_TO_TICKS(250));
    }
}

// =========================================================================
// 7. Power Management & Bluetooth LE (Mi Scale 2 Passive 0x181D Observer)
// =========================================================================
static void bleStartScan();
static std::atomic<bool> s_ble_standby{false};
static std::atomic<bool> s_ble_synced{false};
static std::atomic<unsigned> s_ble_scan_duty{0};
// Scan requests and retries share the NimBLE host queue. The timer never calls
// GAP APIs from a second task, and queued requests use the latest desired mode.
static ble_npl_event s_ble_scan_request;
static ble_npl_callout s_ble_scan_retry_callout;
static BleScanRetry s_ble_scan_retry;
static bool s_ble_applied_standby = false; // Accessed only by the host task.

static void bleRequestScan() {
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_ble_scan_request);
}

static void bleScanRequestEvent(ble_npl_event*) {
    ble_npl_callout_stop(&s_ble_scan_retry_callout);
    s_ble_scan_retry.clear();
    bleStartScan();
}

static void bleScanRetryEvent(ble_npl_event*) {
    // A stopped/replaced callout may already have an event queued.
    if (s_ble_scan_retry.due(clockMs())) bleStartScan();
}

static void bleScheduleScanRetry(const char* operation, int rc) {
    const uint32_t delay = s_ble_scan_retry.failed(clockMs());
    ESP_LOGW(TAG, "BLE scan %s failed: %d; retry in %lu ms", operation, rc,
             static_cast<unsigned long>(delay));
    const int timer_rc = ble_npl_callout_reset(&s_ble_scan_retry_callout,
        // One extra tick prevents an expiry just before the millisecond
        // deadline when the FreeRTOS tick is already partly elapsed.
        ble_npl_time_ms_to_ticks32(delay) + 1);
    ESP_ERROR_CHECK(timer_rc == 0 ? ESP_OK : ESP_FAIL);
}

float bleScanDutyPercent() { return s_ble_scan_duty.load() / 10.0f; }
bool powerModeActive() { return s_power_active.load(); }

static void configureCpuPower(bool active) {
    esp_pm_config_t cfg = {};
    // RTOS runnable-task locks request CPU_MAX too. Cap that maximum at 80
    // in standby, so telemetry/task wakeups do not raise it back to 240 MHz.
    cfg.max_freq_mhz = active ? 240 : 80;
    cfg.min_freq_mhz = 80;
    cfg.light_sleep_enable = true;
    ESP_ERROR_CHECK(esp_pm_configure(&cfg));
}

static void initPowerManagement() {
    // ACTIVE starts with both locks held. STANDBY releases only our own locks;
    // BLE, Wi-Fi, I2C and USB remain responsible for their driver constraints.
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "edge_active_cpu", &s_active_cpu_lock));
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "edge_active", &s_active_sleep_lock));
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "edge_usb", &s_usb_sleep_lock));
    ESP_ERROR_CHECK(esp_pm_lock_acquire(s_active_cpu_lock));
    ESP_ERROR_CHECK(esp_pm_lock_acquire(s_active_sleep_lock));
    ESP_ERROR_CHECK(esp_pm_lock_acquire(s_usb_sleep_lock));
    configureCpuPower(true);
}

static void powerMonitorTask(void* arg) {
    while (true) {
        const bool connected = usb_serial_jtag_is_connected();
        const bool prev = s_usb_sleep_blocked.load();
        if (connected != prev) {
            if (connected) ESP_ERROR_CHECK(esp_pm_lock_acquire(s_usb_sleep_lock));
            else ESP_ERROR_CHECK(esp_pm_lock_release(s_usb_sleep_lock));
            s_usb_sleep_blocked.store(connected);
            if (connected) {
                edgeNotifyControl();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(connected ? 250 : 1000));
    }
}

EdgePowerView powerSnapshot() {
    EdgePowerView out;
    esp_pm_config_t cfg = {};
    if (esp_pm_get_configuration(&cfg) == ESP_OK) {
        out.pmEnabled = true;
        out.lightSleepConfigured = cfg.light_sleep_enable;
        out.minCpuMhz = cfg.min_freq_mhz;
        out.maxCpuMhz = cfg.max_freq_mhz;
    }
    out.cpuMhz = esp_clk_cpu_freq() / 1000000;
    out.usbSleepBlocked = s_usb_sleep_blocked.load();
    out.wifiStarted = s_wifi_started.load();
    out.lcdBacklight = s_lcd_backlight_state.load();
    out.noLightSleepLocks = (s_power_active.load() ? 1 : 0) +
                            (s_usb_sleep_blocked.load() ? 1 : 0);
    wifi_ps_type_t ps;
    if (esp_wifi_get_ps(&ps) == ESP_OK) {
        out.wifiPowerSave = ps == WIFI_PS_NONE ? "NONE" :
                           ps == WIFI_PS_MAX_MODEM ? "MAX_MODEM" : "MIN_MODEM";
    }
    return out;
}

void bleSetLowPowerScan(bool standby) {
    if (s_ble_standby.exchange(standby) == standby) return;
    if (!s_ble_synced.load()) return; // Sync callback starts the selected mode.
    bleRequestScan();
}

void setPowerMode(bool active, bool keepLcdOn) {
    if (s_power_active.load() == active) return;
    if (active) {
        configureCpuPower(true);
        ESP_ERROR_CHECK(esp_pm_lock_acquire(s_active_cpu_lock));
        ESP_ERROR_CHECK(esp_pm_lock_acquire(s_active_sleep_lock));
        xSemaphoreTake(s_lcd_mutex, portMAX_DELAY);
        s_lcd_override_until = 0;
        s_lcd_override_active = false;
        xSemaphoreGive(s_lcd_mutex);
    }
    s_power_active.store(active);
    applyWifiPowerSave(active);
    const bool lcdOn = active || keepLcdOn;
    lcdSetBacklight(lcdOn);
    if (active && s_lcd_wake_sem) {
        xSemaphoreGive(s_lcd_wake_sem);
    }
    bleSetLowPowerScan(!active);
    if (!active) {
        ESP_ERROR_CHECK(esp_pm_lock_release(s_active_sleep_lock));
        ESP_ERROR_CHECK(esp_pm_lock_release(s_active_cpu_lock));
        configureCpuPower(false);
    }
    ESP_LOGI(TAG, "Power phase=%s; BLE duty=%.1f%%; LCD=%s",
             active ? "ACTIVE" : "STANDBY",
             bleScanDutyPercent(),
             lcdOn ? "ON" : "OFF");
}

static int bleGapEventHandler(struct ble_gap_event* event, void* arg) {
    if (event->type == BLE_GAP_EVENT_DISC) {
        uint8_t targetMac[6];
        bool filterMac = false;
        portENTER_CRITICAL(&s_ble_mac_mux);
        filterMac = s_scale_mac_enabled;
        if (filterMac) memcpy(targetMac, s_scale_mac_le, sizeof(targetMac));
        portEXIT_CRITICAL(&s_ble_mac_mux);
        if (filterMac && memcmp(event->disc.addr.val, targetMac, sizeof(targetMac)) != 0) {
            return 0;
        }

        bool sawScale = false;
        const uint8_t* data = event->disc.data;
        const int len = event->disc.length_data;
        int idx = 0;
        while (idx < len) {
            const uint8_t ad_len = data[idx];
            if (ad_len == 0 || idx + ad_len >= len) break;
            const uint8_t ad_type = data[idx + 1];

            // 0x16 = Service Data 16-bit UUID
            if (ad_type == 0x16 && ad_len >= 13) {
                const uint16_t uuid = data[idx + 2] | (data[idx + 3] << 8);
                if (uuid == 0x181D) {
                    const uint8_t* sd = &data[idx + 4];
                    const bool is_lbs = (sd[0] & 0x01) != 0;
                    const bool is_stable = (sd[0] & 0x20) != 0;
                    const bool load_removed = (sd[0] & 0x80) != 0;
                    const uint16_t raw_weight = (sd[2] << 8) | sd[1];

                    portENTER_CRITICAL(&s_state_mux);
                    float kg = raw_weight / (is_lbs ? 100.0f : 200.0f);
                    if (is_lbs) kg *= 0.45359237f;
                    const uint32_t now = clockMs();
                    s_scale_presence.update(now, kg, is_stable, load_removed);
                    if (load_removed || raw_weight == 0) {
                        s_measurements.clearWeight();
                    } else {
                        s_measurements.weight(now, kg, is_stable);
                    }
                    portEXIT_CRITICAL(&s_state_mux);
                    sawScale = true;
                }
            }
            idx += ad_len + 1;
        }
        if (sawScale) {
            edgeNotifyControl();
        }
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        s_ble_scan_duty.store(0);
        bleRequestScan();
    }
    return 0;
}

static void bleStartScan() {
    if (!s_ble_synced.load()) return;
    const bool standby = s_ble_standby.load();
    if (ble_gap_disc_active()) {
        if (s_ble_applied_standby == standby) {
            ble_npl_callout_stop(&s_ble_scan_retry_callout);
            s_ble_scan_retry.clear();
            return;
        }
        const int rc = ble_gap_disc_cancel();
        if (rc != 0 && rc != BLE_HS_EALREADY) {
            bleScheduleScanRetry("cancel", rc);
            return;
        }
    }
    s_ble_scan_duty.store(0);
    struct ble_gap_disc_params disc_params = {};
    disc_params.filter_duplicates = 0;
    disc_params.passive = 1;
    // Use a fast 100 ms interval (160 * 0.625 ms) in standby so all 3 BLE
    // advertising channels (37, 38, 39) are swept within 300 ms for instant wakeup.
    disc_params.itvl = 160;
    disc_params.window = standby ? (160 * EDGE_STANDBY_SCAN_PERCENT / 100) : 160;
#ifndef EDGE_SCALE_ONLY
    // Keep BLE listening during HTTP instead of cancelling discovery. Retain
    // 25 ms window every 200 ms (12.5% duty cycle) in active camera mode.
    if (!standby) { disc_params.itvl = 320; disc_params.window = 40; }
#endif
    const int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &disc_params,
                               bleGapEventHandler, nullptr);
    if (rc == 0) {
        s_ble_applied_standby = standby;
        s_ble_scan_duty.store(1000U * disc_params.window / disc_params.itvl);
        if (s_ble_scan_retry.pending()) {
            ESP_LOGI(TAG, "BLE scan recovered; duty=%.1f%%", bleScanDutyPercent());
        }
        ble_npl_callout_stop(&s_ble_scan_retry_callout);
        s_ble_scan_retry.clear();
        if (usb_serial_jtag_is_connected()) {
            edgeNotifyControl();
        }
    } else if (rc == BLE_HS_EINVAL || rc == BLE_HS_ENOTSUP) {
        // Invalid parameters / unsupported feature need a configuration fix.
        ble_npl_callout_stop(&s_ble_scan_retry_callout);
        s_ble_scan_retry.clear();
        ESP_LOGE(TAG, "BLE scan start failed: %d; check scan configuration", rc);
    } else {
        bleScheduleScanRetry("start", rc);
    }
}

static void bleSyncCallback() {
    s_ble_synced.store(true);
    ESP_LOGI(TAG, "NimBLE host synced; starting passive scale scanner...");
    bleRequestScan();
}

static void bleHostTask(void* param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// =========================================================================
// 8. Master Initialization
// =========================================================================
void edgeIoInit() {
    s_config_mutex = xSemaphoreCreateMutex();
    s_lcd_mutex = xSemaphoreCreateMutex();
    s_control_wake_sem = xSemaphoreCreateBinary();
    s_lcd_wake_sem = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(s_config_mutex && s_lcd_mutex && s_control_wake_sem && s_lcd_wake_sem ? ESP_OK : ESP_ERR_NO_MEM);
    initPowerManagement();

    // 1. NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    loadConfigFromNvs(s_config);

    // 2. USB Serial / JTAG
    // Default tx_buffer_size is only 256 B, which silently drops any write > 256 B.
    s_usb_tx_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_usb_tx_mutex ? ESP_OK : ESP_ERR_NO_MEM);
    usb_serial_jtag_driver_config_t usb_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_cfg.tx_buffer_size = 4096;
    usb_cfg.rx_buffer_size = 1024;
    usb_serial_jtag_driver_install(&usb_cfg);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(powerMonitorTask, "power_monitor", 2048, nullptr, 3, nullptr, 0) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
    xTaskCreatePinnedToCore(usbReceiverTask, "usb_rx", 4096, nullptr, 4, nullptr, 0);

    // 3. LCD & Display Task
    initLcdHardware();
    xTaskCreatePinnedToCore(lcdTask, "lcd_ui", 4096, nullptr, 3, nullptr, 0);

    // 4. Wi-Fi
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t w_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&w_init_cfg);
    esp_timer_create_args_t retry_args = {};
    retry_args.callback = wifiRetryTimer;
    retry_args.name = "wifi_retry";
    ESP_ERROR_CHECK(esp_timer_create(&retry_args, &s_wifi_retry_timer));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(EDGE_WIFI_CONTROL,
        ESP_EVENT_ANY_ID, wifiEventHandler, nullptr, nullptr));
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifiEventHandler, nullptr, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifiEventHandler, nullptr, nullptr);
    esp_wifi_set_mode(WIFI_MODE_STA);
    applyWifiPowerSave(true);
    applyWifiConfig(s_config);

    // 5. Bluetooth LE (NimBLE)
    ESP_ERROR_CHECK(nimble_port_init());
    ble_npl_event_init(&s_ble_scan_request, bleScanRequestEvent, nullptr);
    ESP_ERROR_CHECK(ble_npl_callout_init(&s_ble_scan_retry_callout,
        nimble_port_get_dflt_eventq(), bleScanRetryEvent, nullptr) == 0 ? ESP_OK : ESP_FAIL);
    ble_hs_cfg.sync_cb = bleSyncCallback;
    nimble_port_freertos_init(bleHostTask);
    esp_coex_preference_set(ESP_COEX_PREFER_WIFI);

    ESP_LOGI(TAG, "edge_io subsystem initialized successfully");
}
