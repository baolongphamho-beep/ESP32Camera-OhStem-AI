#include "edge_io.h"
#include "edge_geometry.h"
#include "dl_model_base.hpp"
#include "yolo26.hpp"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static const char* TAG = "edge_yolo";
extern const uint8_t modelStart[] asm("_binary_yolo26n_224_s8_s3_espdl_start");
static constexpr size_t JPEG_CAPACITY = 256 * 1024;

static void status(const char* message) {
    char json[160];
    snprintf(json, sizeof(json), "{\"kind\":\"status\",\"message\":\"%s\"}", message);
    usbFrame(nullptr, 0, json);
}

static size_t capture(const char* url, uint8_t* buffer) {
    esp_http_client_config_t cfg = {};
    cfg.url = url; cfg.timeout_ms = 4000; cfg.buffer_size = 2048;
    auto client = esp_http_client_init(&cfg);
    if (!client) return 0;
    size_t used = 0;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        const auto declared = esp_http_client_fetch_headers(client);
        if (esp_http_client_get_status_code(client) == 200 && declared <= int64_t(JPEG_CAPACITY)) {
            while (used < JPEG_CAPACITY) {
                int n = esp_http_client_read(client, reinterpret_cast<char*>(buffer + used),
                                            std::min(size_t(4096), JPEG_CAPACITY - used));
                if (n <= 0) break;
                used += n;
            }
            if (!esp_http_client_is_complete_data_received(client)) used = 0;
        }
    }
    esp_http_client_close(client); esp_http_client_cleanup(client);
    if (used < 4 || buffer[0] != 0xff || buffer[1] != 0xd8 ||
        buffer[used - 2] != 0xff || buffer[used - 1] != 0xd9) return 0;
    return used;
}

extern "C" void app_main() {
    edgeIoInit();
    lcdMessage("Loading YOLO26n", "Please wait");
    // Keep parameters in flash: copying 3 MB into PSRAM would waste scarce RAM.
    auto* model = new dl::Model(reinterpret_cast<const char*>(modelStart),
        fbs::MODEL_LOCATION_IN_FLASH_RODATA, 0, dl::MEMORY_MANAGER_GREEDY, nullptr, false);
    if (!model->is_built()) {
        lcdMessage("Model RAM error", "Check USB viewer");
        while (true) { status("Model allocation failed"); vTaskDelay(pdMS_TO_TICKS(1000)); }
    }
    YOLO26 detector(model, 16, .5f);
    status("Model loaded; running on-board self-test");
    const auto warmupAt = clockMs();
    for (const auto& input : model->get_inputs()) {
        if (!input.second || !input.second->data) {
            lcdMessage("Model input error", "Check USB viewer");
            while (true) { status("Model input allocation failed"); vTaskDelay(pdMS_TO_TICKS(1000)); }
        }
        memset(input.second->data, 0, input.second->get_bytes());
    }
    model->run(); // Exercise inference RAM even before WiFi has been provisioned.
    char boot[192];
    snprintf(boot, sizeof(boot), "{\"kind\":\"boot\",\"self_test_ms\":%lu,\"free_psram\":%u,\"free_internal\":%u}",
        static_cast<unsigned long>(clockMs() - warmupAt), unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
        unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    usbFrame(nullptr, 0, boot);
    auto* jpeg = static_cast<uint8_t*>(heap_caps_malloc(JPEG_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!jpeg) {
        lcdMessage("JPEG RAM error", "Check PSRAM");
        while (true) { status("JPEG allocation failed"); vTaskDelay(pdMS_TO_TICKS(1000)); }
    }
    ESP_LOGI(TAG, "YOLO26n ready; free PSRAM=%u", unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    while (true) {
        const auto cfg = configSnapshot();
        if (!wifiReady()) {
            cameraState(PersonStatus::None);
            lcdMessage("WiFi not ready", "Setup via USB");
            status("WiFi not ready: configure via USB");
            vTaskDelay(pdMS_TO_TICKS(1000)); continue;
        }
        const auto started = clockMs();
        const size_t length = capture(cfg.cameraUrl, jpeg);
        if (!length) {
            cameraState(PersonStatus::None);
            lcdMessage("Camera offline", "Check IP / WiFi");
            status("Camera capture failed");
            vTaskDelay(pdMS_TO_TICKS(1000)); continue;
        }
        auto img = detector.decode_jpeg(jpeg, length);
        if (!img.data) {
            cameraState(PersonStatus::None); status("JPEG decode failed");
            vTaskDelay(pdMS_TO_TICKS(500)); continue;
        }
        const int width = img.width, height = img.height;
        if (width <= 0 || height <= 0 || width > 1280 || height > 1024) {
            heap_caps_free(img.data); cameraState(PersonStatus::None);
            status("Use QVGA or VGA camera resolution"); continue;
        }
        detector.preprocess(img);
        heap_caps_free(img.data); // Release RGB before inference.
        model->run();
        const auto results = detector.postprocess(model->get_outputs());
        std::vector<PersonBox> people;
        for (const auto& r : results) {
            if (r.category == 0 && r.box.size() >= 4) {
                people.push_back(originalBox({float(r.box[0]), float(r.box[1]),
                    float(r.box[2]), float(r.box[3]), r.score}, width, height));
            }
        }
        PersonStatus person = PersonStatus::None;
        const char* label = "No people detected";
        float cm = 0, distance = 0;
        if (people.size() > 1) { person = PersonStatus::Multiple; label = "Multiple people"; }
        else if (people.size() == 1) {
            const auto& box = people.front();
            person = PersonStatus::Partial; label = "Show head and feet";
            if (fullBox(box, width, height)) {
                person = PersonStatus::NeedsCalibration; label = "Check calibration";
                // Store focal in normalized image-height units, so a resolution
                // change does not silently invalidate it (viewer calls it fy/H).
                float focal = cfg.focalPx * height;
                if (!cfg.groundMode && focal == 0 && std::fabs(cfg.tiltDeg) < .01f) {
                    focal = (box.y2 - box.y1) * cfg.distanceM / (cfg.referenceCm / 100);
                    saveFocalIfUnchanged(cfg, focal / height);
                }
                bool valid = false;
                if (cfg.groundMode) valid = groundHeight(box, width, height, focal,
                    cfg.cameraHeightCm, cfg.tiltDeg, cm, distance);
                else if (focal > 0 && std::fabs(cfg.tiltDeg) < .01f) {
                    distance = cfg.distanceM;
                    cm = 100 * (box.y2 - box.y1) * distance / focal;
                    valid = std::isfinite(cm) && cm >= 30 && cm <= 250 && distance > 0 && distance <= 20;
                }
                if (valid) { person = PersonStatus::Measured; label = "Full body detected"; }
            }
        }
        cameraState(person, cm, distance);
        const auto view = stateSnapshot();
        std::string boxes = "[";
        for (size_t i = 0; i < people.size(); ++i) {
            char item[128]; const auto& b = people[i];
            snprintf(item, sizeof(item), "%s[%.1f,%.1f,%.1f,%.1f,%.3f]",
                i ? "," : "", b.x1, b.y1, b.x2, b.y2, b.score);
            boxes += item;
        }
        boxes += "]";
        char metadata[3500], weight[32] = "null", bmi[32] = "null", h[32] = "null", d[32] = "null";
        if (view.hasWeight) snprintf(weight, sizeof(weight), "%.2f", view.weightKg);
        if (view.hasBMI) snprintf(bmi, sizeof(bmi), "%.2f", view.bmi);
        if (view.hasHeight) { snprintf(h, sizeof(h), "%.1f", cm); snprintf(d, sizeof(d), "%.3f", distance); }
        snprintf(metadata, sizeof(metadata),
            "{\"kind\":\"frame\",\"width\":%d,\"height\":%d,\"status\":\"%s\",\"boxes\":%s,"
            "\"height_cm\":%s,\"distance_m\":%s,\"weight_kg\":%s,\"weight_stable\":%s,\"bmi\":%s,"
            "\"processing_ms\":%lu,\"free_psram\":%u}",
            width, height, label, boxes.c_str(), h, d, weight, view.weightStable ? "true" : "false", bmi,
            static_cast<unsigned long>(clockMs() - started), unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        usbFrame(jpeg, length, metadata);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
