#include "edge_io.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "driver/i2c_master.h"
#include "driver/usb_serial_jtag.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
extern "C" {
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
}

static EdgeConfig config;
static SemaphoreHandle_t configLock, usbLock;
static portMUX_TYPE stateLock = portMUX_INITIALIZER_UNLOCKED;
static MeasurementState state;
static std::atomic<bool> connected{false};
static char overrideLines[2][17] = {};
static bool overrideDisplay = false;
static i2c_master_bus_handle_t lcdBus;
static i2c_master_dev_handle_t lcdDevice;

uint32_t clockMs() { return uint32_t(esp_timer_get_time() / 1000); }

EdgeConfig configSnapshot() {
    xSemaphoreTake(configLock, portMAX_DELAY);
    const EdgeConfig copy = config;
    xSemaphoreGive(configLock);
    return copy;
}

static void persistConfig(const EdgeConfig& value) {
    nvs_handle_t nvs;
    if (nvs_open("smart_edge", NVS_READWRITE, &nvs) != ESP_OK) return;
    nvs_set_blob(nvs, "config_v1", &value, sizeof(value));
    nvs_commit(nvs);
    nvs_close(nvs);
}

void saveFocalIfUnchanged(const EdgeConfig& original, float focal) {
    xSemaphoreTake(configLock, portMAX_DELAY);
    if (config.focalPx == original.focalPx && config.referenceCm == original.referenceCm &&
        config.distanceM == original.distanceM && config.groundMode == original.groundMode) {
        config.focalPx = focal;
        persistConfig(config);
    }
    xSemaphoreGive(configLock);
}

void cameraState(PersonStatus status, float height, float distance) {
    portENTER_CRITICAL(&stateLock);
    state.camera(clockMs(), status, height, distance);
    overrideDisplay = false;
    portEXIT_CRITICAL(&stateLock);
}

MeasurementView stateSnapshot() {
    portENTER_CRITICAL(&stateLock);
    const auto view = state.view(clockMs());
    portEXIT_CRITICAL(&stateLock);
    return view;
}

void lcdMessage(const char* first, const char* second) {
    portENTER_CRITICAL(&stateLock);
    snprintf(overrideLines[0], 17, "%-16.16s", first);
    snprintf(overrideLines[1], 17, "%-16.16s", second);
    overrideDisplay = true;
    portEXIT_CRITICAL(&stateLock);
}

static void expander(uint8_t value) {
    if (lcdDevice) i2c_master_transmit(lcdDevice, &value, 1, 20);
}
static void nibble(uint8_t data, bool rs) {
    const uint8_t value = (data & 0xf0) | 0x08 | (rs ? 1 : 0);
    expander(value | 4);
    esp_rom_delay_us(1);
    expander(value & ~4);
    esp_rom_delay_us(50);
}
static void lcdByte(uint8_t value, bool rs = false) { nibble(value, rs); nibble(value << 4, rs); }

static void lcdInit() {
    const int pairs[][2] = {{11, 12}, {6, 7}};
    for (auto& pair : pairs) {
        i2c_master_bus_config_t bus = {};
        bus.i2c_port = I2C_NUM_0;
        bus.sda_io_num = gpio_num_t(pair[0]);
        bus.scl_io_num = gpio_num_t(pair[1]);
        bus.clk_source = I2C_CLK_SRC_DEFAULT;
        bus.glitch_ignore_cnt = 7;
        bus.flags.enable_internal_pullup = true;
        if (i2c_new_master_bus(&bus, &lcdBus) != ESP_OK) continue;
        int found = -1;
        for (int address = 0x20; address <= 0x3f; ++address) {
            if (address > 0x27 && address < 0x38) continue;
            if (i2c_master_probe(lcdBus, address, 20) == ESP_OK) {
                if (found >= 0) { found = -2; break; }
                found = address;
            }
        }
        if (found >= 0) {
            i2c_device_config_t device = {};
            device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
            device.device_address = found;
            device.scl_speed_hz = 100000;
            if (i2c_master_bus_add_device(lcdBus, &device, &lcdDevice) == ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(50));
                nibble(0x30, false); vTaskDelay(pdMS_TO_TICKS(5));
                nibble(0x30, false); vTaskDelay(pdMS_TO_TICKS(5));
                nibble(0x30, false); nibble(0x20, false);
                lcdByte(0x28); lcdByte(0x0c); lcdByte(0x01);
                vTaskDelay(pdMS_TO_TICKS(3)); lcdByte(0x06);
                ESP_LOGI("LCD", "Ready address=0x%02x SDA=%d SCL=%d", found, pair[0], pair[1]);
                return;
            }
        }
        i2c_del_master_bus(lcdBus); lcdBus = nullptr;
        if (found == -2) { ESP_LOGE("LCD", "Multiple LCD candidates; disconnect extra I2C expanders"); return; }
    }
    ESP_LOGE("LCD", "Not found on GPIO11/12 or GPIO6/7");
}

static void lcdTask(void*) {
    char previous[2][17] = {};
    uint32_t lastProbe = clockMs();
    while (true) {
        if (!lcdDevice && clockMs() - lastProbe >= 5000) {
            lcdInit(); lastProbe = clockMs(); memset(previous, 0, sizeof(previous));
        }
        char lines[2][17] = {};
        bool custom;
        portENTER_CRITICAL(&stateLock);
        custom = overrideDisplay;
        if (custom) memcpy(lines, overrideLines, sizeof(lines));
        portEXIT_CRITICAL(&stateLock);
        if (!custom) {
            const auto v = stateSnapshot();
            if (v.status == PersonStatus::None) {
                snprintf(lines[0], 17, "No people"); snprintf(lines[1], 17, "detected");
            } else if (v.status == PersonStatus::Multiple) {
                snprintf(lines[0], 17, "Multiple people"); snprintf(lines[1], 17, "One at a time");
            } else if ((clockMs() / 2500) % 2 == 0) {
                if (v.hasHeight) snprintf(lines[0], 17, "H:%.1fcm", double(v.heightCm));
                else snprintf(lines[0], 17, "Need calibration");
                if (v.hasWeight) snprintf(lines[1], 17, "W:%.2fkg %s", double(v.weightKg), v.weightStable ? "LOCK" : "...");
                else snprintf(lines[1], 17, "W:-- wait scale");
            } else {
                if (v.hasHeight) snprintf(lines[0], 17, "D:%.2fm", double(v.distanceM));
                else snprintf(lines[0], 17, "Full body/setup");
                if (v.hasBMI) snprintf(lines[1], 17, "BMI:%.2f", double(v.bmi));
                else snprintf(lines[1], 17, "BMI:-- wait lock");
            }
        }
        if (lcdDevice) for (int row = 0; row < 2; ++row) {
            const size_t count = strnlen(lines[row], 16);
            memset(lines[row] + count, ' ', 16 - count); lines[row][16] = 0;
            if (strcmp(lines[row], previous[row])) {
                lcdByte(row ? 0xc0 : 0x80);
                for (unsigned column = 0; column < 16; ++column) lcdByte(lines[row][column], true);
                memcpy(previous[row], lines[row], 17);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static uint32_t crcUpdate(uint32_t crc, const uint8_t* data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & -(crc & 1));
    }
    return crc;
}

void usbFrame(const uint8_t* jpeg, size_t length, const char* metadata) {
    if (!usb_serial_jtag_is_connected()) return;
    const uint32_t jsonLength = strlen(metadata);
    if (length > 256 * 1024 || jsonLength > 4096) return;
    struct Header { char magic[4]; uint32_t jsonLength, jpegLength, crc; } header = {{'S','S','V','1'}, jsonLength, uint32_t(length), 0};
    uint32_t crc = crcUpdate(0xffffffff, reinterpret_cast<const uint8_t*>(metadata), jsonLength);
    if (length) crc = crcUpdate(crc, jpeg, length);
    header.crc = crc ^ 0xffffffff;
    xSemaphoreTake(usbLock, portMAX_DELAY);
    auto write = [](const void* bytes, size_t size) {
        auto cursor = static_cast<const uint8_t*>(bytes);
        while (size) {
            const size_t chunk = std::min(size, size_t(2048));
            int sent = usb_serial_jtag_write_bytes(cursor, chunk, pdMS_TO_TICKS(30));
            if (sent <= 0) return false;
            cursor += sent; size -= sent;
        }
        return true;
    };
    if (write(&header, sizeof(header)) && write(metadata, jsonLength) && length) write(jpeg, length);
    xSemaphoreGive(usbLock);
}

static void wifiApply() {
    const auto cfg = configSnapshot();
    if (!cfg.ssid[0]) return;
    wifi_config_t wifi = {};
    memcpy(wifi.sta.ssid, cfg.ssid, std::min(sizeof(wifi.sta.ssid), strlen(cfg.ssid)));
    memcpy(wifi.sta.password, cfg.password, std::min(sizeof(wifi.sta.password), strlen(cfg.password)));
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &wifi);
    esp_wifi_connect();
}

static void wifiEvent(void*, esp_event_base_t base, int32_t event, void*) {
    if (base == IP_EVENT && event == IP_EVENT_STA_GOT_IP) {
        connected.store(true); ESP_LOGI("WIFI", "Connected");
    } else if (base == WIFI_EVENT && event == WIFI_EVENT_STA_START) wifiApply();
    else if (base == WIFI_EVENT && event == WIFI_EVENT_STA_DISCONNECTED) {
        connected.store(false);
        if (configSnapshot().ssid[0]) esp_wifi_connect();
    }
}
bool wifiReady() { return connected.load(); }

static void usbCommandTask(void*) {
    char line[512]; size_t used = 0; bool overflow = false;
    EdgeConfig pending = configSnapshot();
    while (true) {
        char ch;
        if (usb_serial_jtag_read_bytes(&ch, 1, pdMS_TO_TICKS(100)) <= 0) continue;
        if (ch == '\r') continue;
        if (ch != '\n') {
            if (used < sizeof(line) - 1 && !overflow) line[used++] = ch;
            else overflow = true;
            continue;
        }
        line[used] = 0; used = 0;
        if (overflow) { overflow = false; continue; }
        if (!strcmp(line, "BEGIN")) {
            pending = configSnapshot();
        } else if (!strcmp(line, "APPLY")) {
            const bool valid = std::isfinite(pending.distanceM) && pending.distanceM > 0 &&
                std::isfinite(pending.referenceCm) && pending.referenceCm >= 30 && pending.referenceCm <= 250 &&
                std::isfinite(pending.focalPx) && pending.focalPx >= 0 &&
                std::isfinite(pending.tiltDeg) && fabsf(pending.tiltDeg) < 80 &&
                (!pending.groundMode || (std::isfinite(pending.cameraHeightCm) && pending.cameraHeightCm > 0));
            if (!valid) { usbFrame(nullptr, 0, "{\"kind\":\"config\",\"ok\":false}"); continue; }
            const auto old = configSnapshot();
            xSemaphoreTake(configLock, portMAX_DELAY); config = pending; persistConfig(config); xSemaphoreGive(configLock);
            cameraState(PersonStatus::None);
            if (strcmp(old.ssid, pending.ssid) || strcmp(old.password, pending.password)) wifiApply();
            usbFrame(nullptr, 0, "{\"kind\":\"config\",\"ok\":true}");
        } else if (!strncmp(line, "CFG ", 4)) {
            char* equal = strchr(line + 4, '=');
            if (!equal) continue;
            *equal = 0;
            const char* key = line + 4; const char* value = equal + 1;
            if (!strcmp(key, "ssid")) snprintf(pending.ssid, sizeof(pending.ssid), "%s", value);
            else if (!strcmp(key, "password")) snprintf(pending.password, sizeof(pending.password), "%s", value);
            else if (!strcmp(key, "camera")) snprintf(pending.cameraUrl, sizeof(pending.cameraUrl), "%s", value);
            else if (!strcmp(key, "mac")) snprintf(pending.scaleMac, sizeof(pending.scaleMac), "%s", value);
            else if (!strcmp(key, "mode")) pending.groundMode = !strcmp(value, "ground");
            else {
                char* end; float number = strtof(value, &end);
                if (end == value || *end || !std::isfinite(number)) continue;
                if (!strcmp(key, "distance")) pending.distanceM = number;
                else if (!strcmp(key, "reference")) pending.referenceCm = number;
                else if (!strcmp(key, "focal")) pending.focalPx = number;
                else if (!strcmp(key, "height")) pending.cameraHeightCm = number;
                else if (!strcmp(key, "tilt")) pending.tiltDeg = number;
            }
        }
        // Password commands are deliberately never logged or echoed.
    }
}

static int gapEvent(ble_gap_event* event, void*);
static void scanStart() {
    uint8_t addressType;
    if (ble_hs_id_infer_auto(0, &addressType)) return;
    ble_gap_disc_params params = {};
    params.passive = 1; params.itvl = 160; params.window = 80; params.filter_duplicates = 0;
    ble_gap_disc(addressType, BLE_HS_FOREVER, &params, gapEvent, nullptr);
}

static int gapEvent(ble_gap_event* event, void*) {
    if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) { scanStart(); return 0; }
    if (event->type != BLE_GAP_EVENT_DISC) return 0;
    const auto cfg = configSnapshot();
    unsigned bytes[6];
    if (sscanf(cfg.scaleMac, "%x:%x:%x:%x:%x:%x", &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]) != 6) return 0;
    for (int i = 0; i < 6; ++i) if (bytes[i] != event->disc.addr.val[5-i]) return 0;
    ble_hs_adv_fields fields = {};
    if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data)) return 0;
    const uint8_t* service = fields.svc_data_uuid16;
    if (!service || fields.svc_data_uuid16_len < 12 || service[0] != 0x1d || service[1] != 0x18) return 0;
    const uint8_t* data = service + 2;
    const unsigned raw = unsigned(data[1]) | (unsigned(data[2]) << 8);
    portENTER_CRITICAL(&stateLock);
    if ((data[0] & 0x80) || raw == 0) state.clearWeight();
    else {
        const float kg = (data[0] & 1) ? raw / 100.0f * 0.45359237f : raw / 200.0f;
        state.weight(clockMs(), kg, (data[0] & 0x20) != 0);
    }
    portEXIT_CRITICAL(&stateLock);
    return 0;
}

static void bleTask(void*) { nimble_port_run(); nimble_port_freertos_deinit(); }

void edgeIoInit() {
    configLock = xSemaphoreCreateMutex(); usbLock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(nvs_flash_init()); // Never erase an existing partition automatically.
    nvs_handle_t nvs; size_t size = sizeof(config);
    if (nvs_open("smart_edge", NVS_READONLY, &nvs) == ESP_OK) {
        EdgeConfig stored;
        if (nvs_get_blob(nvs, "config_v1", &stored, &size) == ESP_OK && size == sizeof(stored)) config = stored;
        nvs_close(nvs);
    }
    usb_serial_jtag_driver_config_t usb = {.tx_buffer_size = 4096, .rx_buffer_size = 1024};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
    xTaskCreate(usbCommandTask, "usb_config", 6144, nullptr, 3, nullptr);
    lcdInit(); lcdMessage("Loading YOLO26n", "Please wait...");
    xTaskCreate(lcdTask, "lcd", 4096, nullptr, 2, nullptr);
    ESP_ERROR_CHECK(esp_netif_init()); ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t wifi = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifiEvent, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifiEvent, nullptr));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = scanStart;
    nimble_port_freertos_init(bleTask);
}
