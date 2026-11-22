#include "edge_io.h"
#include "camera_test.h"
#include "servo_jog.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>
#include <cmath>
#include <atomic>
#include "esp_pm.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "wifi_credentials.h"
#include "wifi_reconnect.h"
#include "ble_scan_retry.h"
#include "esp_coexist.h"
#include <string_view>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
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
static ScaleReading s_scale;
static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;

void cameraState(PersonStatus status, float height, float distance) {
    portENTER_CRITICAL(&s_state_mux);
    s_measurements.camera(clockMs(), status, height, distance);
    portEXIT_CRITICAL(&s_state_mux);
}

MeasurementView stateSnapshot() {
    portENTER_CRITICAL(&s_state_mux);
    MeasurementView view = s_measurements.view(clockMs());
    portEXIT_CRITICAL(&s_state_mux);
    return view;
}

// =========================================================================
// 3. Configuration & NVS Management
// =========================================================================
static EdgeDeviceConfig s_config;
static SemaphoreHandle_t s_config_mutex = nullptr;
static SemaphoreHandle_t s_usb_tx_mutex = nullptr;
static bool s_in_config_session = false, s_config_ok = true;
static EdgeDeviceConfig s_staging_config;

// Store individual fields rather than ABI-dependent struct blobs.
static void loadConfigFromNvs(EdgeDeviceConfig& cfg) {
    nvs_handle_t h;
    if (nvs_open("edge_cfg", NVS_READONLY, &h) != ESP_OK) return;
    size_t n = sizeof(cfg.ssid); nvs_get_str(h, "ssid", cfg.ssid, &n);
    n = sizeof(cfg.password); nvs_get_str(h, "pass", cfg.password, &n);
    n = sizeof(cfg.cameraUrl); nvs_get_str(h, "camera", cfg.cameraUrl, &n);
    n = sizeof(cfg.scaleMac); nvs_get_str(h, "mac", cfg.scaleMac, &n);
    auto get = [&](const char* key, auto& value) {
        size_t size = sizeof(value); nvs_get_blob(h, key, &value, &size);
    };
    // Storage types of TV3 servo_ctrl.zip: servo_pin/servo_dir i32, servo_settle/servo_delay
    // u32, angles/speeds float blobs. Before 22/11 the LAB stored pin/dir/settle as blobs:
    // read those once as a fallback (rewritten in the new types on the next APPLY).
    int32_t i32 = 0;
    uint32_t u32 = 0;
    if (nvs_get_i32(h, "servo_pin", &i32) == ESP_OK) cfg.servoPin = i32;
    else get("servo_pin", cfg.servoPin);
    if (nvs_get_i32(h, "servo_dir", &i32) == ESP_OK) cfg.dir = (i32 < 0) ? -1 : 1;
    else {
        int legacy = 0;
        size_t size = sizeof(legacy);
        // Legacy blob used tilt = dir * (level - command): the opposite sign of TV3's.
        if (nvs_get_blob(h, "servo_dir", &legacy, &size) == ESP_OK && (legacy == 1 || legacy == -1))
            cfg.dir = -legacy;
    }
    if (nvs_get_u32(h, "servo_settle", &u32) == ESP_OK) cfg.settleMs = u32;
    else get("servo_settle", cfg.settleMs);
    if (nvs_get_u32(h, "servo_delay", &u32) == ESP_OK) cfg.frameDelayMs = u32;
    get("servo_bottom", cfg.bottomDeg); get("servo_top", cfg.topDeg);
    get("servo_step", cfg.stepDeg); get("servo_level", cfg.levelDeg);
    get("servo_dps", cfg.speedDps); get("hold_sec", cfg.holdSec);
    get("focal", cfg.focal); get("cam_h", cfg.cameraHeightCm);
    get("cam_framesize", cfg.camFramesize); get("cam_quality", cfg.camQuality);
    nvs_close(h);
    if (!deviceConfigValid(cfg)) {
        // Older fixed-camera calibration may contain zero focal/height.
        EdgeDeviceConfig defaults;
        std::strcpy(defaults.ssid, cfg.ssid);
        std::strcpy(defaults.password, cfg.password);
        if (!std::strncmp(cfg.cameraUrl, "http://", 7) && cfg.cameraUrl[7])
            std::strcpy(defaults.cameraUrl, cfg.cameraUrl);
        char mac[18];
        if (sweep_config_detail::normalizeMac(cfg.scaleMac, mac)) std::strcpy(defaults.scaleMac, mac);
        cfg = defaults;
        ESP_LOGW(TAG, "Invalid/legacy geometry: Plan 2 defaults restored; configure via USB");
    }
}

static bool saveConfigToNvs(const EdgeDeviceConfig& cfg) {
    nvs_handle_t h;
    if (nvs_open("edge_cfg", NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = true;
    auto str = [&](const char* key, const char* value) { if (nvs_set_str(h, key, value) != ESP_OK) ok = false; };
    auto put = [&](const char* key, const auto& value) {
        if (nvs_set_blob(h, key, &value, sizeof(value)) != ESP_OK) ok = false;
    };
    str("ssid", cfg.ssid); str("pass", cfg.password);
    str("camera", cfg.cameraUrl); str("mac", cfg.scaleMac);
    // TV3 types (see loadConfigFromNvs). nvs_set_* of another type replaces a legacy blob.
    for (const char* key : {"servo_pin", "servo_dir", "servo_settle"}) nvs_erase_key(h, key);
    if (nvs_set_i32(h, "servo_pin", cfg.servoPin) != ESP_OK) ok = false;
    if (nvs_set_i32(h, "servo_dir", cfg.dir) != ESP_OK) ok = false;
    if (nvs_set_u32(h, "servo_settle", cfg.settleMs) != ESP_OK) ok = false;
    if (nvs_set_u32(h, "servo_delay", cfg.frameDelayMs) != ESP_OK) ok = false;
    put("servo_bottom", cfg.bottomDeg); put("servo_top", cfg.topDeg);
    put("servo_step", cfg.stepDeg); put("servo_level", cfg.levelDeg);
    put("servo_dps", cfg.speedDps); put("hold_sec", cfg.holdSec);
    put("focal", cfg.focal); put("cam_h", cfg.cameraHeightCm);
    put("cam_framesize", cfg.camFramesize); put("cam_quality", cfg.camQuality);
    if (ok) ok = nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

EdgeDeviceConfig configSnapshot() {
    xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    const auto copy = s_config;
    xSemaphoreGive(s_config_mutex);
    return copy;
}

ScaleReading scaleSnapshot() {
    portENTER_CRITICAL(&s_state_mux);
    auto copy = s_scale;
    portEXIT_CRITICAL(&s_state_mux);
    // Same upper bound as MeasurementState. Load-removed clears immediately.
    if (clockMs() - copy.at > MeasurementState::WEIGHT_TTL_MS) copy.present = copy.stable = false;
    return copy;
}

// =========================================================================
// 4. Wi-Fi Station
// =========================================================================
// Ported from the team GitHub smartscale-edge-ai@78548a6 (22/11): every Wi-Fi policy
// change (USB APPLY, retry timer) is posted to the default event loop, so connect /
// set_config / retry never race with the Wi-Fi driver events; a disconnect retries
// after 2 s instead of a tight connect loop; credentials are checked by
// wifi_credentials.h (32-byte SSID / 64-hex keys kept whole, WPA/WPA2 or open).
static std::atomic<bool> s_wifi_ready{false};
static std::atomic<bool> s_wifi_started{false};
static std::atomic<int> s_power_mode{-1};
static char s_wifi_ip[16] = "no IP";
static portMUX_TYPE s_ip_mux = portMUX_INITIALIZER_UNLOCKED;
ESP_EVENT_DEFINE_BASE(EDGE_WIFI_CONTROL);
enum { WIFI_CONTROL_CONFIG, WIFI_CONTROL_RETRY };
struct WifiControlConfig { char ssid[33]; char password[65]; };
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
    if (s_wifi_retry_timer) esp_timer_stop(s_wifi_retry_timer);  // An inactive timer is harmless.
}

static void wifiRetryTimer(void*) {
    // Never connect from the timer task: post a wakeup; the event loop re-checks the deadline.
    if (esp_event_post(EDGE_WIFI_CONTROL, WIFI_CONTROL_RETRY, nullptr, 0, 0) != ESP_OK)
        esp_timer_start_once(s_wifi_retry_timer, 100000);
}

static void wifiArmRetry() {
    if (!s_wifi_reconnect.pending() || !s_wifi_retry_timer) return;
    esp_timer_stop(s_wifi_retry_timer);
    const uint32_t wait = std::max<uint32_t>(1, s_wifi_reconnect.remaining(clockMs()));
    const esp_err_t rc = esp_timer_start_once(s_wifi_retry_timer, uint64_t(wait) * 1000);
    if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "Wi-Fi retry timer: %s", esp_err_to_name(rc));
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

static void wifiApplyPowerSave() {
    // With BLE enabled ESP-IDF may refuse WIFI_PS_NONE; log instead of aborting.
    const esp_err_t ps = esp_wifi_set_ps(s_power_mode.load() == 0 ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
    if (ps != ESP_OK) ESP_LOGW(TAG, "esp_wifi_set_ps failed: %s", esp_err_to_name(ps));
}

static void wifiStartDesiredConfig() {
    if (!s_wifi_reconnect.enabled()) return;
    esp_err_t rc = esp_wifi_set_config(WIFI_IF_STA, &s_wifi_desired_config);
    if (rc == ESP_OK) rc = esp_wifi_start();
    s_wifi_started.store(rc == ESP_OK);
    if (rc == ESP_OK) wifiApplyPowerSave();
    else ESP_LOGE(TAG, "Wi-Fi start failed: %s", esp_err_to_name(rc));
}

static void wifiEventHandler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data) {
    if (event_base == EDGE_WIFI_CONTROL && event_id == WIFI_CONTROL_CONFIG) {
        const auto& cfg = *static_cast<const WifiControlConfig*>(event_data);
        WifiCredentials credentials;
        const char* error = prepareWifiCredentials(
            std::string_view(cfg.ssid, strnlen(cfg.ssid, sizeof(cfg.ssid))),
            std::string_view(cfg.password, strnlen(cfg.password, sizeof(cfg.password))), credentials);
        if (error) {
            ESP_LOGE(TAG, "Wi-Fi configuration rejected: %s", error);  // Never logs the credentials.
            return;  // Keep the current connection for an invalid request.
        }
        wifiCancelRetry();
        wifiClearReady();
        s_wifi_reconnect.enable(cfg.ssid[0] != '\0');
        if (!s_wifi_reconnect.enabled()) ESP_LOGW(TAG, "No Wi-Fi SSID configured; waiting for USB configuration");
        s_wifi_desired_config = {};
        static_assert(sizeof(s_wifi_desired_config.sta.ssid) == sizeof(credentials.ssid), "ssid");
        static_assert(sizeof(s_wifi_desired_config.sta.password) == sizeof(credentials.password), "password");
        memcpy(s_wifi_desired_config.sta.ssid, credentials.ssid.data(), credentials.ssid.size());
        memcpy(s_wifi_desired_config.sta.password, credentials.password.data(), credentials.password.size());
        s_wifi_desired_config.sta.threshold.authmode = credentials.open ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_WPA2_PSK;
        if (s_wifi_waiting_stop) return;  // Keep the newest config until STOP.
        if (s_wifi_started.load()) {
            // STOP is an ordering fence: stale disconnect/start events are ignored until it arrives.
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
        else wifiArmRetry();  // A stale timer event must not connect early.
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
        wifiCancelRetry();  // Associated: wait for DHCP, do not start another connect.
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
    }
}

void wifiIp(char* out, size_t size) {
    if (!out || !size) return;
    portENTER_CRITICAL(&s_ip_mux);
    snprintf(out, size, "%s", s_wifi_ip);
    portEXIT_CRITICAL(&s_ip_mux);
}

static void applyWifiConfig(const EdgeDeviceConfig& cfg) {
    // Called by init / the USB task, never from the event loop. The event copies the
    // credentials; all connect/config/timer changes are serialized on that loop.
    WifiControlConfig copy = {};
    std::memcpy(copy.ssid, cfg.ssid, sizeof(copy.ssid));
    std::memcpy(copy.password, cfg.password, sizeof(copy.password));
    if (esp_event_post(EDGE_WIFI_CONTROL, WIFI_CONTROL_CONFIG, &copy, sizeof(copy), portMAX_DELAY) != ESP_OK)
        ESP_LOGE(TAG, "Wi-Fi configuration event could not be queued");
    std::memset(copy.password, 0, sizeof(copy.password));
}

// RSSI of the camera AP in dBm; 0 when not connected (sent as null).
int wifiRssi() {
    if (!wifiReady()) return 0;
    wifi_ap_record_t ap = {};
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
}

bool wifiReady() {
    return s_wifi_ready.load();
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

static uint32_t calculateCrc32(const uint8_t* data, size_t length, uint32_t prev_crc = 0) {
    uint32_t crc = ~prev_crc;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

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
        const int n = usb_serial_jtag_write_bytes(
            ptr + sent, chunk, pdMS_TO_TICKS(std::min<int32_t>(remain, 25)));
        if (n > 0) {
            sent += static_cast<size_t>(n);
        } else {
            return false;
        }
    }
    return true;
}

void usbFrame(const uint8_t* jpeg, size_t length, const char* metadata) {
    if (!metadata) return;
    const uint32_t json_len = static_cast<uint32_t>(strlen(metadata));
    const uint32_t jpeg_len = (jpeg && length > 0) ? static_cast<uint32_t>(length) : 0;

    uint32_t crc = calculateCrc32(reinterpret_cast<const uint8_t*>(metadata), json_len, 0);
    if (jpeg_len > 0) {
        crc = calculateCrc32(jpeg, jpeg_len, crc);
    }

    UsbPacketHeader hdr;
    memcpy(hdr.magic, "SSV1", 4);
    hdr.json_len = json_len;
    hdr.jpeg_len = jpeg_len;
    hdr.crc = crc;

    if (s_usb_tx_mutex) xSemaphoreTake(s_usb_tx_mutex, portMAX_DELAY);
    if (usbWriteAll(&hdr, sizeof(hdr), 100) && usbWriteAll(metadata, json_len, 200) && jpeg_len > 0) {
        usbWriteAll(jpeg, jpeg_len, 1500);
    }
    if (s_usb_tx_mutex) xSemaphoreGive(s_usb_tx_mutex);
}

static CameraTestLease s_camera_test;
bool cameraTestEnabled() { return s_camera_test.active(clockMs()); }
static ServoJogLease s_servo_jog;
bool servoJogTarget(float& deg) {
    if (!s_servo_jog.active(clockMs())) return false;
    deg = s_servo_jog.deg();
    return true;
}

static void processConfigLine(const std::string& line) {
    float jogDeg = 0;
    const auto jog = parseServoJog(line.c_str(), jogDeg);
    if (jog != ServoJogCommand::None) {
        char reply[96];
        if (jog == ServoJogCommand::Move) {
            s_servo_jog.renew(clockMs(), jogDeg);
            snprintf(reply, sizeof(reply), "{\"kind\":\"servo_jog\",\"ok\":true,\"active\":true,\"deg\":%.1f}",
                     s_servo_jog.deg());
        } else if (jog == ServoJogCommand::Off) {
            s_servo_jog.stop();
            snprintf(reply, sizeof(reply), "{\"kind\":\"servo_jog\",\"ok\":true,\"active\":false}");
        } else {
            snprintf(reply, sizeof(reply), "{\"kind\":\"servo_jog\",\"ok\":false,\"error\":\"SERVO %.0f..%.0f or OFF\"}",
                     double(SWEEP_LIMIT_MIN_DEG), double(SWEEP_LIMIT_MAX_DEG));
        }
        usbFrame(nullptr, 0, reply);
        return;
    }
    if (line == "CONFIG?") {
        // Read-back of the values in use (NVS after boot/APPLY); never the Wi-Fi password.
        char json[768];
        const auto cfg = configSnapshot();
        if (deviceConfigJson(cfg, json, sizeof(json))) usbFrame(nullptr, 0, json);
        else usbFrame(nullptr, 0, "{\"kind\":\"config_values\",\"error\":\"too long\"}");
        return;
    }
    if (line == "PREVIEW ON" || line == "PREVIEW OFF") {
        if (line == "PREVIEW ON") s_camera_test.renew(clockMs());
        else s_camera_test.stop();
        usbFrame(nullptr, 0, line == "PREVIEW ON" ?
            "{\"kind\":\"preview\",\"ok\":true,\"active\":true}" :
            "{\"kind\":\"preview\",\"ok\":true,\"active\":false}");
    } else if (line == "BEGIN") {
        s_staging_config = configSnapshot();
        s_in_config_session = s_config_ok = true;
    } else if (s_in_config_session && line.rfind("CFG ", 0) == 0) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) { s_config_ok = false; return; }
        const auto key = line.substr(4, eq - 4), value = line.substr(eq + 1);
        const auto result = applyDeviceConfigKey(s_staging_config, key.c_str(), value.c_str());
        if (result == ConfigResult::Invalid || result == ConfigResult::Unknown) s_config_ok = false;
    } else if (s_in_config_session && line == "APPLY") {
        s_in_config_session = false;
        if (!s_config_ok || !deviceConfigValid(s_staging_config)) {
            usbFrame(nullptr, 0, "{\"kind\":\"config\",\"ok\":false,\"error\":\"Invalid Plan 2 configuration\"}");
            return;
        }
        xSemaphoreTake(s_config_mutex, portMAX_DELAY);
        const bool wifi_changed = std::strcmp(s_config.ssid, s_staging_config.ssid) ||
                                  std::strcmp(s_config.password, s_staging_config.password);
        const bool mac_changed = std::strcmp(s_config.scaleMac, s_staging_config.scaleMac);
        const bool saved = saveConfigToNvs(s_staging_config);
        if (saved) { s_staging_config.revision = s_config.revision + 1; s_config = s_staging_config; }
        xSemaphoreGive(s_config_mutex);
        if (saved && mac_changed) {
            portENTER_CRITICAL(&s_state_mux);
            s_scale = {}; s_measurements.clearWeight(clockMs());
            portEXIT_CRITICAL(&s_state_mux);
        }
        if (saved && wifi_changed) applyWifiConfig(s_staging_config);
        usbFrame(nullptr, 0, saved ? "{\"kind\":\"config\",\"ok\":true}" :
                                    "{\"kind\":\"config\",\"ok\":false,\"error\":\"NVS write failed\"}");
    }
}

static void usbReceiverTask(void* pvParameters) {
    uint8_t buf[256];
    std::string line_accum;
    while (true) {
        const int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(50));
        for (int i = 0; i < n; ++i) {
            const char c = static_cast<char>(buf[i]);
            if (c == '\n' || c == '\r') {
                if (!line_accum.empty()) {
                    processConfigLine(line_accum);
                    line_accum.clear();
                }
            } else {
                if (line_accum.size() < 256) line_accum.push_back(c);
                else s_config_ok = false;
            }
        }
    }
}

// =========================================================================
// 6. LCD 16x2 (PCF8574 over I2C)
// =========================================================================
static uint8_t s_lcd_addr = 0x21; // Default lab address
static bool s_lcd_ready = false;
static bool s_backlight = true;
static char s_lcd_msg_first[17] = {};
static char s_lcd_msg_second[17] = {};
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

static void pcfWriteNibble(uint8_t nibble, bool is_data) {
    const uint8_t b = (nibble & 0xF0) | (is_data ? 0x01 : 0x00) | (s_backlight ? 0x08 : 0);
    uint8_t seq[2] = {
        static_cast<uint8_t>(b | 0x04),  // Enable HIGH
        static_cast<uint8_t>(b & ~0x04)  // Enable LOW
    };
    i2cTransmit(seq, sizeof(seq));
}

static void lcdWriteByte(uint8_t val, bool is_data) {
    pcfWriteNibble(val & 0xF0, is_data);
    pcfWriteNibble((val << 4) & 0xF0, is_data);
}

static void lcdSetLine(uint8_t row, const std::string& text) {
    lcdWriteByte(row == 0 ? 0x80 : 0xC0, false);
    for (size_t i = 0; i < 16; ++i) {
        const char ch = (i < text.size()) ? text[i] : ' ';
        lcdWriteByte(static_cast<uint8_t>(ch), true);
    }
}

// HD44780 4-bit (re)initialization. Three 0x30 nibbles bring the controller back to
// 8-bit mode from ANY state, so this also repairs a nibble desync (garbage characters)
// caused by a glitch, e.g. a servo current spike. esp_rom_delay_us(): with
// CONFIG_FREERTOS_HZ=100, pdMS_TO_TICKS(1..9) is 0 ticks and would not wait at all.
static void lcdInitSequence() {
    pcfWriteNibble(0x30, false);
    esp_rom_delay_us(4500);       // > 4.1 ms
    pcfWriteNibble(0x30, false);
    esp_rom_delay_us(150);        // > 100 us
    pcfWriteNibble(0x30, false);
    esp_rom_delay_us(150);
    pcfWriteNibble(0x20, false);  // 4-bit mode
    esp_rom_delay_us(150);
    lcdWriteByte(0x28, false);    // 2 lines, 5x8 font
    lcdWriteByte(0x0C, false);    // Display ON, Cursor OFF
    lcdWriteByte(0x06, false);    // Entry mode increment
    lcdWriteByte(0x01, false);    // Clear display
    esp_rom_delay_us(2000);       // > 1.52 ms
}

static void initLcdHardware() {
#if defined(USE_I2C_MASTER_NEW)
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = I2C_NUM_0;
    bus_cfg.sda_io_num = static_cast<gpio_num_t>(11);
    bus_cfg.scl_io_num = static_cast<gpio_num_t>(12);
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;  // Same as the legacy branch (backpacks often lack pull-ups).
    if (i2c_new_master_bus(&bus_cfg, &s_i2c_bus) != ESP_OK) return;

    // Scan for PCF8574 backpacks (0x20..0x27, 0x38..0x3F)
    const uint8_t candidates[] = {0x21, 0x27, 0x20, 0x3F, 0x38};
    for (uint8_t addr : candidates) {
        i2c_device_config_t dev_cfg = {};
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address = addr;
        dev_cfg.scl_speed_hz = 100000;
        if (i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_i2c_dev) == ESP_OK) {
            uint8_t probe = 0x08;
            if (i2c_master_transmit(s_i2c_dev, &probe, 1, 30) == ESP_OK) {
                s_lcd_addr = addr;
                s_lcd_ready = true;
                ESP_LOGI(TAG, "LCD detected at I2C address 0x%02X", s_lcd_addr);
                break;
            }
            i2c_master_bus_rm_device(s_i2c_dev);
            s_i2c_dev = nullptr;
        }
    }
#elif defined(USE_I2C_MASTER_LEGACY)
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = 11;
    conf.scl_io_num = 12;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = 100000;
    i2c_param_config(I2C_NUM_0, &conf);
    i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
    s_lcd_ready = true;
#endif

    if (!s_lcd_ready) {
        ESP_LOGW(TAG, "LCD backpack not found on SDA=11 SCL=12");
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(50));  // > 40 ms after power-up (5 ticks at 100 Hz)
    lcdInitSequence();
}

void lcdSetBacklight(bool on) {
    xSemaphoreTake(s_lcd_mutex, portMAX_DELAY);
    if (s_backlight != on) {
        s_backlight = on;
        const uint8_t value = on ? 0x08 : 0;
        if (s_lcd_ready) i2cTransmit(&value, 1);
        if (on && s_lcd_ready) {
            // Every wake from STANDBY: resync the controller, then redraw the cached text.
            lcdInitSequence();
            lcdSetLine(0, s_lcd_msg_first);
            lcdSetLine(1, s_lcd_msg_second);
        }
    }
    xSemaphoreGive(s_lcd_mutex);
}

void lcdMessage(const char* first, const char* second) {
    xSemaphoreTake(s_lcd_mutex, portMAX_DELAY);
    char rows[2][17];
    std::snprintf(rows[0], 17, "%-16.16s", first ? first : "");
    std::snprintf(rows[1], 17, "%-16.16s", second ? second : "");
    if (std::strcmp(rows[0], s_lcd_msg_first)) {
        std::strcpy(s_lcd_msg_first, rows[0]);
        if (s_lcd_ready) lcdSetLine(0, rows[0]);
    }
    if (std::strcmp(rows[1], s_lcd_msg_second)) {
        std::strcpy(s_lcd_msg_second, rows[1]);
        if (s_lcd_ready) lcdSetLine(1, rows[1]);
    }
    xSemaphoreGive(s_lcd_mutex);
}

LcdLines lcdShowPhase(SystemPhase phase, EdgeNotice notice, const LcdData& data) {
    const auto lines = lcdTextFor(phase, notice, data);
    lcdMessage(lines.line[0], lines.line[1]);
    return lines;
}

// =========================================================================
// 7. Bluetooth LE (Mi Scale 2 Advertisement Observer)
// =========================================================================
static void bleStartScan();
static void bleRequestScan();
static std::atomic<bool> s_ble_synced{false}, s_ble_standby{true};
static std::atomic<bool> s_ble_camera_test{false};
static esp_pm_lock_handle_t s_cpu_lock = nullptr;

static int bleGapEventHandler(struct ble_gap_event* event, void* arg) {
    if (event->type == BLE_GAP_EVENT_DISC) {
        const auto cfg = configSnapshot();
        if (!scaleAddressMatches(event->disc.addr.val, cfg.scaleMac)) return 0;
        ScaleReading reading;
        if (!decodeScaleAdvertisement(event->disc.data, event->disc.length_data, reading)) return 0;
        reading.at = clockMs();
        portENTER_CRITICAL(&s_state_mux);
        s_scale = reading;
        if (reading.present) s_measurements.weight(reading.at, reading.kg, reading.stable);
        else s_measurements.clearWeight(reading.at);
        portEXIT_CRITICAL(&s_state_mux);
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) bleRequestScan();
    return 0;
}

// Ported from the team GitHub smartscale-edge-ai@78548a6 (22/11): scan requests from any
// task are queued to the NimBLE host task (GAP APIs are called only there), failed
// start/cancel retries after 100 ms .. 2 s (ble_scan_retry.h), and standby scans every
// 100 ms (all 3 advertising channels within ~300 ms) for a fast wake at the same 10 % duty.
static ble_npl_event s_ble_scan_request;
static ble_npl_callout s_ble_scan_retry_callout;
static BleScanRetry s_ble_scan_retry;
static int s_ble_applied_mode = -1;  // Host task only: 0 active, 1 standby, 2 stopped (preview).

static void bleRequestScan() {
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_ble_scan_request);
}

static void bleScheduleScanRetry(const char* operation, int rc) {
    const uint32_t delay = s_ble_scan_retry.failed(clockMs());
    ESP_LOGW(TAG, "BLE scan %s failed: %d; retry in %lu ms", operation, rc, static_cast<unsigned long>(delay));
    ble_npl_callout_reset(&s_ble_scan_retry_callout, ble_npl_time_ms_to_ticks32(delay) + 1);
}

static void bleStartScan() {  // NimBLE host task only.
    if (!s_ble_synced) return;
    const int mode = s_ble_camera_test ? 2 : (s_ble_standby ? 1 : 0);
    if (ble_gap_disc_active()) {
        if (s_ble_applied_mode == mode) {
            ble_npl_callout_stop(&s_ble_scan_retry_callout);
            s_ble_scan_retry.clear();
            return;
        }
        const int rc = ble_gap_disc_cancel();
        if (rc != 0 && rc != BLE_HS_EALREADY) { bleScheduleScanRetry("cancel", rc); return; }
    }
    if (mode == 2) {  // Preview: no scale scanning.
        s_ble_applied_mode = 2;
        ble_npl_callout_stop(&s_ble_scan_retry_callout);
        s_ble_scan_retry.clear();
        return;
    }
    ble_gap_disc_params params = {};
    params.filter_duplicates = 0; params.passive = 1;
    // Standby: 10 ms every 100 ms (10 %, fast wake); active: 25 ms every 200 ms (12.5 %),
    // so Wi-Fi capture keeps most of the shared antenna time.
    params.itvl = mode == 1 ? 160 : 320;
    params.window = mode == 1 ? 16 : 40;
    const int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &params, bleGapEventHandler, nullptr);
    if (rc == 0) {
        if (s_ble_scan_retry.pending()) ESP_LOGI(TAG, "BLE scan recovered");
        s_ble_applied_mode = mode;
        ble_npl_callout_stop(&s_ble_scan_retry_callout);
        s_ble_scan_retry.clear();
    } else if (rc == BLE_HS_EINVAL || rc == BLE_HS_ENOTSUP) {
        ble_npl_callout_stop(&s_ble_scan_retry_callout);
        s_ble_scan_retry.clear();
        ESP_LOGE(TAG, "BLE scan start failed: %d; check scan configuration", rc);
    } else {
        bleScheduleScanRetry("start", rc);
    }
}

static void bleScanRequestEvent(ble_npl_event*) {
    ble_npl_callout_stop(&s_ble_scan_retry_callout);
    s_ble_scan_retry.clear();
    bleStartScan();
}

static void bleScanRetryEvent(ble_npl_event*) {
    if (s_ble_scan_retry.due(clockMs())) bleStartScan();  // A replaced callout may still fire.
}

void bleSetLowPowerScan(bool standby) {
    if (s_ble_standby.exchange(standby) == standby || !s_ble_synced) return;
    bleRequestScan();
}

void bleSetCameraTest(bool enabled) {
    if (s_ble_camera_test.exchange(enabled) == enabled || !s_ble_synced) return;
    bleRequestScan();
}

void bleSetCapturePriority(bool capturing) {
    // Keep observing the scale during HTTP: pausing BLE could lose load-removed.
    // ESP-IDF Wi-Fi/BLE coexistence arbitrates the shared antenna.
    (void)capturing;
}

void setPowerMode(bool active) {
    const int previous = s_power_mode.exchange(int(active));
    if (previous == int(active)) return;
    if (s_cpu_lock) {
        if (active) esp_pm_lock_acquire(s_cpu_lock);
        else if (previous == 1) esp_pm_lock_release(s_cpu_lock);
    }
    // With BLE enabled ESP-IDF may refuse WIFI_PS_NONE (coexistence keeps modem sleep);
    // that adds DTIM wake-up delay to every camera fetch, so make it visible in the log.
    const esp_err_t ps = esp_wifi_set_ps(active ? WIFI_PS_NONE : WIFI_PS_MIN_MODEM);
    if (ps != ESP_OK) ESP_LOGW(TAG, "esp_wifi_set_ps(%s) failed: %s", active ? "NONE" : "MIN_MODEM",
                               esp_err_to_name(ps));
    bleSetLowPowerScan(!active);
    lcdSetBacklight(active);
}

static void bleSyncCallback() {
    ESP_LOGI(TAG, "NimBLE host synced; starting passive scale scanner...");
    s_ble_synced = true;
    bleStartScan();
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
    s_usb_tx_mutex = xSemaphoreCreateMutex();
    s_lcd_mutex = xSemaphoreCreateMutex();

    // 1. NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    loadConfigFromNvs(s_config);

    // 2. USB Serial / JTAG
    // Default tx_buffer_size is only 256 B, which silently drops any write > 256 B.
    usb_serial_jtag_driver_config_t usb_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_cfg.tx_buffer_size = 4096;
    usb_cfg.rx_buffer_size = 1024;
    usb_serial_jtag_driver_install(&usb_cfg);

    // 3. LCD & Display Task
    initLcdHardware();
    // LCD is owned by the Plan 2 FSM; no competing display task.

    // 4. Wi-Fi
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t w_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&w_init_cfg);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifiEventHandler, nullptr, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifiEventHandler, nullptr, nullptr);
    esp_event_handler_instance_register(EDGE_WIFI_CONTROL, ESP_EVENT_ANY_ID, wifiEventHandler, nullptr, nullptr);
    esp_timer_create_args_t retry_args = {};
    retry_args.callback = wifiRetryTimer;
    retry_args.name = "wifi_retry";
    if (esp_timer_create(&retry_args, &s_wifi_retry_timer) != ESP_OK) ESP_LOGE(TAG, "Wi-Fi retry timer not created");
    esp_wifi_set_mode(WIFI_MODE_STA);
    applyWifiConfig(s_config);

    // 5. Bluetooth LE (NimBLE)
    nimble_port_init();
    ble_npl_event_init(&s_ble_scan_request, bleScanRequestEvent, nullptr);
    ble_npl_callout_init(&s_ble_scan_retry_callout, nimble_port_get_dflt_eventq(), bleScanRetryEvent, nullptr);
    ble_hs_cfg.sync_cb = bleSyncCallback;
    nimble_port_freertos_init(bleHostTask);
    esp_coex_preference_set(ESP_COEX_PREFER_WIFI);  // Camera capture first; BLE still scans.

    esp_pm_config_t pm = {};
    pm.max_freq_mhz = 240; pm.min_freq_mhz = 80;
    pm.light_sleep_enable = false; // USB/BLE wake latency must be measured before enabling.
    if (esp_pm_configure(&pm) == ESP_OK)
        esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "plan2-active", &s_cpu_lock);
    else ESP_LOGW(TAG, "DFS unavailable; standby still gates camera/AI/LCD/servo");
    xTaskCreate(usbReceiverTask, "usb_rx", 4096, nullptr, 4, nullptr);
    ESP_LOGI(TAG, "Plan 2 edge_io ready");
}
