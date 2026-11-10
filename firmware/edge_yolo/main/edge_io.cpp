#include "edge_io.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>
#include <cmath>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
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
static EdgeConfig s_config;
static SemaphoreHandle_t s_config_mutex = nullptr;
static bool s_in_config_session = false;
static EdgeConfig s_staging_config;

static void loadConfigFromNvs(EdgeConfig& cfg) {
    nvs_handle_t h;
    if (nvs_open("edge_cfg", NVS_READONLY, &h) != ESP_OK) return;

    size_t sz = sizeof(cfg.ssid);
    nvs_get_str(h, "ssid", cfg.ssid, &sz);
    sz = sizeof(cfg.password);
    nvs_get_str(h, "pass", cfg.password, &sz);
    sz = sizeof(cfg.cameraUrl);
    nvs_get_str(h, "camera", cfg.cameraUrl, &sz);
    sz = sizeof(cfg.scaleMac);
    nvs_get_str(h, "mac", cfg.scaleMac, &sz);

    uint8_t gm = 0;
    if (nvs_get_u8(h, "gmode", &gm) == ESP_OK) cfg.groundMode = (gm != 0);

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

    nvs_close(h);
    ESP_LOGI(TAG, "Configuration loaded from NVS: camera=%s mode=%s",
             cfg.cameraUrl, cfg.groundMode ? "ground" : "fixed");
}

static void saveConfigToNvs(const EdgeConfig& cfg) {
    nvs_handle_t h;
    if (nvs_open("edge_cfg", NVS_READWRITE, &h) != ESP_OK) return;

    nvs_set_str(h, "ssid", cfg.ssid);
    nvs_set_str(h, "pass", cfg.password);
    nvs_set_str(h, "camera", cfg.cameraUrl);
    nvs_set_str(h, "mac", cfg.scaleMac);
    nvs_set_u8(h, "gmode", cfg.groundMode ? 1 : 0);
    nvs_set_blob(h, "dist", &cfg.distanceM, sizeof(float));
    nvs_set_blob(h, "ref_cm", &cfg.referenceCm, sizeof(float));
    nvs_set_blob(h, "focal", &cfg.focalPx, sizeof(float));
    nvs_set_blob(h, "cam_h", &cfg.cameraHeightCm, sizeof(float));
    nvs_set_blob(h, "tilt", &cfg.tiltDeg, sizeof(float));

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
    xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    if (std::fabs(s_config.focalPx - original.focalPx) < 0.0001f) {
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
// 4. Wi-Fi Station
// =========================================================================
static bool s_wifi_ready = false;
static bool s_wifi_started = false;

static void wifiEventHandler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_ready = false;
        ESP_LOGW(TAG, "Wi-Fi disconnected; retrying in 2 seconds...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto* event = reinterpret_cast<ip_event_got_ip_t*>(event_data);
        ESP_LOGI(TAG, "Wi-Fi connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_ready = true;
    }
}

static void applyWifiConfig(const EdgeConfig& cfg) {
    if (cfg.ssid[0] == '\0') {
        ESP_LOGW(TAG, "No Wi-Fi SSID configured; waiting for USB configuration");
        return;
    }
    wifi_config_t wifi_cfg = {};
    memset(wifi_cfg.sta.ssid, 0, sizeof(wifi_cfg.sta.ssid));
    memset(wifi_cfg.sta.password, 0, sizeof(wifi_cfg.sta.password));
    memcpy(wifi_cfg.sta.ssid, cfg.ssid, (sizeof(wifi_cfg.sta.ssid) - 1 < sizeof(cfg.ssid)) ? sizeof(wifi_cfg.sta.ssid) - 1 : sizeof(cfg.ssid));
    memcpy(wifi_cfg.sta.password, cfg.password, (sizeof(wifi_cfg.sta.password) - 1 < sizeof(cfg.password)) ? sizeof(wifi_cfg.sta.password) - 1 : sizeof(cfg.password));
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (!s_wifi_started) {
        s_wifi_started = true;
        esp_wifi_start();
    } else {
        esp_wifi_connect();
    }
}

bool wifiReady() {
    return s_wifi_ready;
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

    usb_serial_jtag_write_bytes(&hdr, sizeof(hdr), pdMS_TO_TICKS(50));
    usb_serial_jtag_write_bytes(metadata, json_len, pdMS_TO_TICKS(100));
    if (jpeg_len > 0) {
        usb_serial_jtag_write_bytes(jpeg, jpeg_len, pdMS_TO_TICKS(500));
    }
}

static void processConfigLine(const std::string& line) {
    if (line == "BEGIN") {
        s_staging_config = configSnapshot();
        s_in_config_session = true;
    } else if (s_in_config_session && line.rfind("CFG ", 0) == 0) {
        const size_t eq = line.find('=');
        if (eq != std::string::npos) {
            const std::string key = line.substr(4, eq - 4);
            const std::string val = line.substr(eq + 1);
            if (key == "ssid") strncpy(s_staging_config.ssid, val.c_str(), sizeof(s_staging_config.ssid) - 1);
            else if (key == "password") strncpy(s_staging_config.password, val.c_str(), sizeof(s_staging_config.password) - 1);
            else if (key == "camera") strncpy(s_staging_config.cameraUrl, val.c_str(), sizeof(s_staging_config.cameraUrl) - 1);
            else if (key == "mac") strncpy(s_staging_config.scaleMac, val.c_str(), sizeof(s_staging_config.scaleMac) - 1);
            else if (key == "mode") s_staging_config.groundMode = (val == "ground");
            else if (key == "distance") s_staging_config.distanceM = strtof(val.c_str(), nullptr);
            else if (key == "reference") s_staging_config.referenceCm = strtof(val.c_str(), nullptr);
            else if (key == "focal") s_staging_config.focalPx = strtof(val.c_str(), nullptr);
            else if (key == "height") s_staging_config.cameraHeightCm = strtof(val.c_str(), nullptr);
            else if (key == "tilt") s_staging_config.tiltDeg = strtof(val.c_str(), nullptr);
        }
    } else if (s_in_config_session && line == "APPLY") {
        s_in_config_session = false;
        xSemaphoreTake(s_config_mutex, portMAX_DELAY);
        const bool wifi_changed = (strcmp(s_config.ssid, s_staging_config.ssid) != 0 ||
                                  strcmp(s_config.password, s_staging_config.password) != 0);
        s_config = s_staging_config;
        saveConfigToNvs(s_config);
        xSemaphoreGive(s_config_mutex);

        if (wifi_changed) {
            applyWifiConfig(s_staging_config);
        }
        usbFrame(nullptr, 0, "{\"kind\":\"config\",\"ok\":true}");
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
            }
        }
    }
}

// =========================================================================
// 6. LCD 16x2 (PCF8574 over I2C)
// =========================================================================
static uint8_t s_lcd_addr = 0x21; // Default lab address
static bool s_lcd_ready = false;
static char s_lcd_msg_first[17] = {};
static char s_lcd_msg_second[17] = {};
static uint32_t s_lcd_override_until = 0;
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
    const uint8_t b = (nibble & 0xF0) | (is_data ? 0x01 : 0x00) | 0x08; // 0x08 = Backlight
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

static void initLcdHardware() {
#if defined(USE_I2C_MASTER_NEW)
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = I2C_NUM_0;
    bus_cfg.sda_io_num = static_cast<gpio_num_t>(11);
    bus_cfg.scl_io_num = static_cast<gpio_num_t>(12);
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
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

    // HD44780 4-bit initialization sequence
    vTaskDelay(pdMS_TO_TICKS(50));
    pcfWriteNibble(0x30, false);
    vTaskDelay(pdMS_TO_TICKS(5));
    pcfWriteNibble(0x30, false);
    vTaskDelay(pdMS_TO_TICKS(1));
    pcfWriteNibble(0x30, false);
    pcfWriteNibble(0x20, false); // 4-bit mode

    lcdWriteByte(0x28, false); // 2 lines, 5x8 font
    lcdWriteByte(0x0C, false); // Display ON, Cursor OFF
    lcdWriteByte(0x06, false); // Entry mode increment
    lcdWriteByte(0x01, false); // Clear display
    vTaskDelay(pdMS_TO_TICKS(5));
}

void lcdMessage(const char* first, const char* second) {
    xSemaphoreTake(s_lcd_mutex, portMAX_DELAY);
    strncpy(s_lcd_msg_first, first ? first : "", sizeof(s_lcd_msg_first) - 1);
    strncpy(s_lcd_msg_second, second ? second : "", sizeof(s_lcd_msg_second) - 1);
    s_lcd_override_until = clockMs() + 2000;
    if (s_lcd_ready) {
        lcdSetLine(0, s_lcd_msg_first);
        lcdSetLine(1, s_lcd_msg_second);
    }
    xSemaphoreGive(s_lcd_mutex);
}

static void lcdTask(void* pvParameters) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(250));
        if (!s_lcd_ready) continue;

        xSemaphoreTake(s_lcd_mutex, portMAX_DELAY);
        if (clockMs() < s_lcd_override_until) {
            xSemaphoreGive(s_lcd_mutex);
            continue;
        }

        const auto view = stateSnapshot();
        const uint32_t phase = (clockMs() / 2500) % 2;

        if (view.status == PersonStatus::None) {
            lcdSetLine(0, "No people");
            lcdSetLine(1, "detected");
        } else if (view.status == PersonStatus::Multiple) {
            lcdSetLine(0, "Multiple people");
            lcdSetLine(1, "One at a time");
        } else if (view.status == PersonStatus::NeedsCalibration) {
            lcdSetLine(0, "Person detected");
            lcdSetLine(1, "Need calibration");
        } else if (view.status == PersonStatus::Partial) {
            lcdSetLine(0, "Person detected");
            lcdSetLine(1, "Full body needed");
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
        } else {
            char l0[20], l1[20];
            if (view.hasHeight) snprintf(l0, sizeof(l0), "D:%.2fm", view.distanceM);
            else snprintf(l0, sizeof(l0), "D:--");

            if (view.hasBMI) snprintf(l1, sizeof(l1), "BMI:%.2f", view.bmi);
            else snprintf(l1, sizeof(l1), "BMI:-- wait lock");

            lcdSetLine(0, l0);
            lcdSetLine(1, l1);
        }
        xSemaphoreGive(s_lcd_mutex);
    }
}

// =========================================================================
// 7. Bluetooth LE (Mi Scale 2 Advertisement Observer)
// =========================================================================
static void bleStartScan();

static int bleGapEventHandler(struct ble_gap_event* event, void* arg) {
    if (event->type == BLE_GAP_EVENT_DISC) {
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
                    if (load_removed || raw_weight == 0) {
                        s_measurements.clearWeight();
                    } else {
                        float kg = raw_weight / (is_lbs ? 100.0f : 200.0f);
                        if (is_lbs) kg *= 0.45359237f;
                        s_measurements.weight(clockMs(), kg, is_stable);
                    }
                    portEXIT_CRITICAL(&s_state_mux);
                }
            }
            idx += ad_len + 1;
        }
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        bleStartScan();
    }
    return 0;
}

static void bleStartScan() {
    struct ble_gap_disc_params disc_params = {};
    disc_params.filter_duplicates = 0;
    disc_params.passive = 1;
    disc_params.itvl = 1600; // 1000ms
    disc_params.window = 1600;
    ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &disc_params, bleGapEventHandler, nullptr);
}

static void bleSyncCallback() {
    ESP_LOGI(TAG, "NimBLE host synced; starting passive scale scanner...");
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
    s_lcd_mutex = xSemaphoreCreateMutex();

    // 1. NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    loadConfigFromNvs(s_config);

    // 2. USB Serial / JTAG
    usb_serial_jtag_driver_config_t usb_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&usb_cfg);
    xTaskCreate(usbReceiverTask, "usb_rx", 4096, nullptr, 4, nullptr);

    // 3. LCD & Display Task
    initLcdHardware();
    xTaskCreate(lcdTask, "lcd_ui", 3072, nullptr, 3, nullptr);

    // 4. Wi-Fi
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t w_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&w_init_cfg);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifiEventHandler, nullptr, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifiEventHandler, nullptr, nullptr);
    esp_wifi_set_mode(WIFI_MODE_STA);
    applyWifiConfig(s_config);

    // 5. Bluetooth LE (NimBLE)
    nimble_port_init();
    ble_hs_cfg.sync_cb = bleSyncCallback;
    nimble_port_freertos_init(bleHostTask);

    ESP_LOGI(TAG, "edge_io subsystem initialized successfully");
}
