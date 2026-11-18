#include "edge_io.h"
#include "edge_geometry.h"
#include "dl_model_base.hpp"
#include "yolo26.hpp"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static const char* TAG = "edge_yolo";
extern const uint8_t modelStart[] asm("_binary_yolo26n_224_s8_s3_espdl_start");
static constexpr size_t JPEG_CAPACITY = 256 * 1024;

static void status(const char* message) {
    // Keep the JSON valid even when the message quotes a camera reply.
    char safe[400];
    size_t j = 0;
    for (size_t i = 0; message[i] && j + 1 < sizeof(safe); ++i) {
        const unsigned char c = static_cast<unsigned char>(message[i]);
        safe[j++] = (c == '"') ? '\'' : (c == '\\') ? '/' : (c < 0x20 || c > 0x7e) ? '.' : char(c);
    }
    safe[j] = '\0';
    char json[480];
    snprintf(json, sizeof(json), "{\"kind\":\"status\",\"message\":\"%s\"}", safe);
    usbFrame(nullptr, 0, json);
}

// Failure reasons: full text for the USB viewer, short (<=16 chars) for the LCD.
struct CaptureError { char detail[160] = ""; char lcd[17] = ""; };

static void captureFail(CaptureError& err, const char* lcd, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(err.detail, sizeof(err.detail), fmt, args);
    va_end(args);
    snprintf(err.lcd, sizeof(err.lcd), "%s", lcd);
}

static size_t capture(const char* url, uint8_t* buffer, CaptureError& err) {
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.timeout_ms = 2200;
    cfg.buffer_size = 4096;
    cfg.buffer_size_tx = 1024;
    auto client = esp_http_client_init(&cfg);
    if (!client) {
        captureFail(err, "Bad camera URL", "Invalid camera URL '%s'", url);
        return 0;
    }

    size_t used = 0;
    int64_t declared = -1;
    int code = 0;
    bleSetCapturePriority(true);
    const esp_err_t opened = esp_http_client_open(client, 0);
    if (opened != ESP_OK) {
        captureFail(err, "Cam: no connect", "Cannot connect to camera (%s)", esp_err_to_name(opened));
    } else {
        declared = esp_http_client_fetch_headers(client);
        code = esp_http_client_get_status_code(client);
        if (declared < 0) {
            captureFail(err, "Cam: no reply", declared == -ESP_ERR_HTTP_EAGAIN ?
                "Camera accepted TCP but sent no HTTP reply in 2.2 s (busy/weak Wi-Fi?)" :
                "Camera closed the connection without an HTTP reply");
        } else if (code != 200) {
            captureFail(err, "Cam: HTTP error", "Camera replied HTTP %d (is /capture the right path?)", code);
        } else if (declared > int64_t(JPEG_CAPACITY)) {
            captureFail(err, "Cam: image big", "Image %lld B > %u B: use QVGA/VGA",
                        static_cast<long long>(declared), unsigned(JPEG_CAPACITY));
        } else {
            const size_t limit = (declared > 0) ? static_cast<size_t>(declared) : JPEG_CAPACITY;
            while (used < limit) {
                int n = esp_http_client_read(client, reinterpret_cast<char*>(buffer + used),
                                            std::min(size_t(8192), limit - used));
                if (n <= 0) break;
                used += n;
                // When Content-Length is omitted (chunked/EOF), stop as soon as
                // the JPEG EOI marker (0xFFD9) arrives instead of waiting for FIN.
                if (declared <= 0 && used >= 4 && buffer[0] == 0xff && buffer[1] == 0xd8 &&
                    buffer[used - 2] == 0xff && buffer[used - 1] == 0xd9) {
                    break;
                }
            }
        }
    }
    const bool complete_http = (opened == ESP_OK && declared >= 0 && code == 200 &&
                                esp_http_client_is_complete_data_received(client));
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    bleSetCapturePriority(false);

    if (!used) {
        if (opened == ESP_OK && declared >= 0 && code == 200) {
            captureFail(err, "Cam: cut image", "Image transfer incomplete after 0 B (timeout/weak Wi-Fi)");
        }
        return 0;
    }

    // Tolerate padding after the JPEG end marker (some camera drivers add it).
    size_t end = used;
    while (end >= 2 && used - end < 1024 && !(buffer[end - 2] == 0xff && buffer[end - 1] == 0xd9)) --end;
    const bool valid_jpeg = (used >= 4 && buffer[0] == 0xff && buffer[1] == 0xd8 &&
                             end >= 4 && buffer[end - 2] == 0xff && buffer[end - 1] == 0xd9);

    if (!valid_jpeg) {
        if (!complete_http) {
            captureFail(err, "Cam: cut image", "Image transfer incomplete after %u B (timeout/weak Wi-Fi)",
                        unsigned(used));
            return 0;
        }
        char head[25] = {};
        const size_t shown = std::min(used, sizeof(head) - 1);
        for (size_t i = 0; i < shown; ++i) {
            head[i] = (buffer[i] >= 0x20 && buffer[i] < 0x7f) ? char(buffer[i]) : '.';
        }
        // e.g. {"busy":1} when a browser tab/other script is using the camera.
        captureFail(err, "Cam: busy/notJPG", "Reply is not a JPEG (%u B, starts '%s'): close other camera clients",
                    unsigned(used), head);
        return 0;
    }
    return end;
}

// Double-buffered background capture state so Core 0 downloads frame N+1 over
// Wi-Fi while Core 1 decodes and runs YOLO26n inference on frame N.
static uint8_t* s_jpeg_buffers[2] = {nullptr, nullptr};
static SemaphoreHandle_t s_capture_mutex = nullptr;
static SemaphoreHandle_t s_capture_sem = nullptr;
static int s_ready_idx = -1;
static int s_busy_idx = -1;
static size_t s_ready_len = 0;
static uint32_t s_ready_capture_ms = 0;
static uint32_t s_consecutive_failures = 0;
static CaptureError s_last_error;

static void captureWorkerTask(void* arg) {
    while (true) {
        if (!wifiReady()) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        int write_idx = -1;
        xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
        if (s_ready_idx == -1) {
            write_idx = (s_busy_idx == 0) ? 1 : 0;
        }
        xSemaphoreGive(s_capture_mutex);

        if (write_idx < 0) {
            // A fresh frame is already staged and waiting for inference; yield
            // so BLE scanning and Wi-Fi stay cool until inference consumes it.
            vTaskDelay(pdMS_TO_TICKS(15));
            continue;
        }

        const auto cfg = configSnapshot();
        const auto started = clockMs();
        CaptureError err;
        const size_t length = capture(cfg.cameraUrl, s_jpeg_buffers[write_idx], err);
        const uint32_t elapsed = clockMs() - started;

        xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
        if (length > 0) {
            s_ready_idx = write_idx;
            s_ready_len = length;
            s_ready_capture_ms = elapsed;
            s_consecutive_failures = 0;
            xSemaphoreGive(s_capture_mutex);
            xSemaphoreGive(s_capture_sem);
            // Brief breather for camera esp_httpd & BLE advertisement window.
            vTaskDelay(pdMS_TO_TICKS(30));
        } else {
            ++s_consecutive_failures;
            s_last_error = err;
            const uint32_t fails = s_consecutive_failures;
            xSemaphoreGive(s_capture_mutex);
            if (fails >= 3) {
                xSemaphoreGive(s_capture_sem);
            }
            // Retry quickly on transient 1-2 packet Wi-Fi glitches; back off only
            // when the camera is persistently unreachable.
            const uint32_t retry_ms = (fails == 1) ? 80 : (fails == 2) ? 250 :
                                      std::min<uint32_t>(2500, 600 + fails * 300);
            vTaskDelay(pdMS_TO_TICKS(retry_ms));
        }
    }
}

extern "C" void app_main() {
    edgeIoInit();
    lcdMessage("Loading YOLO26n", "Please wait");
    // Copy model weights into 8-bit Octal PSRAM (80 MHz DDR) for faster
    // inference; fall back to Flash rodata only if PSRAM is tight.
    auto* model = new dl::Model(reinterpret_cast<const char*>(modelStart),
        fbs::MODEL_LOCATION_IN_FLASH_RODATA, 0, dl::MEMORY_MANAGER_GREEDY, nullptr, true);
    if (!model->is_built()) {
        delete model;
        model = new dl::Model(reinterpret_cast<const char*>(modelStart),
            fbs::MODEL_LOCATION_IN_FLASH_RODATA, 0, dl::MEMORY_MANAGER_GREEDY, nullptr, false);
    }
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

    s_jpeg_buffers[0] = static_cast<uint8_t*>(heap_caps_malloc(JPEG_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    s_jpeg_buffers[1] = static_cast<uint8_t*>(heap_caps_malloc(JPEG_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!s_jpeg_buffers[0] || !s_jpeg_buffers[1]) {
        lcdMessage("JPEG RAM error", "Check PSRAM");
        while (true) { status("JPEG allocation failed"); vTaskDelay(pdMS_TO_TICKS(1000)); }
    }
    s_capture_mutex = xSemaphoreCreateMutex();
    s_capture_sem = xSemaphoreCreateBinary();
    // Pin background HTTP capture to Core 0 (alongside Wi-Fi/lwIP) so Core 1
    // can run YOLO26n inference uninterrupted in parallel.
    xTaskCreatePinnedToCore(captureWorkerTask, "cam_cap", 6144, nullptr, 4, nullptr, 0);

    ESP_LOGI(TAG, "YOLO26n ready; free PSRAM=%u", unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    while (true) {
        if (!wifiReady()) {
            cameraState(PersonStatus::None);
            lcdMessage("WiFi not ready", "Setup via USB");
            status("WiFi not ready: configure via USB");
            vTaskDelay(pdMS_TO_TICKS(1000)); continue;
        }

        xSemaphoreTake(s_capture_sem, pdMS_TO_TICKS(1000));

        int buf_idx = -1;
        size_t length = 0;
        uint32_t captureMs = 0;
        uint32_t fails = 0;
        CaptureError err;

        xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
        if (s_ready_idx >= 0) {
            buf_idx = s_ready_idx;
            length = s_ready_len;
            captureMs = s_ready_capture_ms;
            s_busy_idx = buf_idx;
            s_ready_idx = -1;
        } else {
            fails = s_consecutive_failures;
            err = s_last_error;
        }
        xSemaphoreGive(s_capture_mutex);

        const auto cfg = configSnapshot();
        if (buf_idx < 0 || !length) {
            // Only report offline after 3+ consecutive failures so brief 1-2
            // packet Wi-Fi hiccups do not interrupt the live display or LCD.
            if (fails >= 3) {
                char ip[16];
                wifiIp(ip, sizeof(ip));
                cameraState(PersonStatus::None);
                if (fails % 2) lcdMessage("Camera offline", err.lcd);
                else lcdMessage("UNO IP:", ip);
                char text[400];
                snprintf(text, sizeof(text), "Camera capture failed: %s | URL=%s | UNO IP=%s",
                         err.detail, cfg.cameraUrl, ip);
                status(text);
                ESP_LOGW(TAG, "%s", text);
            }
            continue;
        }

        uint8_t* jpeg = s_jpeg_buffers[buf_idx];
        const auto inferStarted = clockMs();
        auto img = detector.decode_jpeg(jpeg, length);
        if (!img.data) {
            xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
            s_busy_idx = -1;
            xSemaphoreGive(s_capture_mutex);
            continue;
        }
        const int width = img.width, height = img.height;
        if (width <= 0 || height <= 0 || width > 1280 || height > 1024) {
            heap_caps_free(img.data);
            xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
            s_busy_idx = -1;
            xSemaphoreGive(s_capture_mutex);
            cameraState(PersonStatus::None);
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
            "\"processing_ms\":%lu,\"capture_ms\":%lu,\"free_psram\":%u}",
            width, height, label, boxes.c_str(), h, d, weight, view.weightStable ? "true" : "false", bmi,
            static_cast<unsigned long>(clockMs() - inferStarted),
            static_cast<unsigned long>(captureMs),
            unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        usbFrame(jpeg, length, metadata);
        xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
        s_busy_idx = -1;
        xSemaphoreGive(s_capture_mutex);
    }
}
