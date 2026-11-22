#include "edge_io.h"
#include "edge_geometry.h"
#include "capture_gate.h"
#include "servo_ctrl.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "dl_model_base.hpp"
#include "yolo26.hpp"
#include "esp_heap_caps.h"
#include "http_capture_client.h"
#include "esp_log.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static const char* TAG = "edge_yolo";
#define EDGE_STRINGIFY_IMPL(x) #x
#define EDGE_STRINGIFY(x) EDGE_STRINGIFY_IMPL(x)
extern const uint8_t modelStart[] asm("_binary_yolo26n_" EDGE_STRINGIFY(EDGE_MODEL_INPUT) "_s8_s3_espdl_start");
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

// Camera download: HttpCaptureClient from the team GitHub (http_capture_client.h) replaces
// esp_http_client (22/11). Owned by the capture worker task only.
static HttpCaptureClient s_http;

// Two PSRAM buffers and a worker on Core 0 retain upstream's separate HTTP task.
// Plan 2 requests each image AFTER the servo has settled; speculative prefetch
// across a servo move would associate old pixels with a new tilt angle.
struct CaptureRequest { uint32_t epoch; char url[160]; int framesize = -1, quality = -1; };

// One short GET /control?var=..&val=.. on the same host as the capture URL; true on HTTP 200.
static bool pushCameraSetting(const char* captureUrl, const char* var, int value) {
    const bool ok = s_http.applyControl(captureUrl, var, value);
    ESP_LOGI(TAG, "[CAM] %s=%d %s", var, value, ok ? "OK" : "FAILED");
    return ok;
}

// Re-push framesize/quality when the URL or a value changes (or after a camera
// reboot is suspected: a failed capture). Failed pushes retry at most every 10 s.
static void applyCameraSettings(const CaptureRequest& r, camera_control::Applied& applied,
                                uint32_t& lastAttempt, bool captureFailed) {
    if (r.framesize < 0 && r.quality < 0) return;
    if (captureFailed) applied = {};
    if (applied.same(r.url, r.framesize, r.quality)) return;
    const uint32_t now = clockMs();
    if (lastAttempt && now - lastAttempt < 10000) return;
    lastAttempt = now;
    bool ok = true;
    if (r.framesize >= 0) ok = pushCameraSetting(r.url, "framesize", r.framesize) && ok;
    if (r.quality >= 0) ok = pushCameraSetting(r.url, "quality", r.quality) && ok;
    if (ok) {
        applied.set(r.url, r.framesize, r.quality);
        lastAttempt = 0;
        vTaskDelay(pdMS_TO_TICKS(150));  // Let the sensor produce a frame at the new size.
    }
    char msg[96];
    snprintf(msg, sizeof(msg), "Camera settings framesize=%d quality=%d: %s",
             r.framesize, r.quality, ok ? "applied" : "camera did not accept (retry in 10 s)");
    status(msg);
}
struct CaptureReply {
    uint32_t epoch = 0, captureMs = 0;
    int index = 0;
    size_t length = 0;
    CaptureError error;
};
static uint8_t* s_jpeg_buffers[2] = {};
static QueueHandle_t s_requests, s_replies;

static void captureWorkerTask(void*) {
    int index = 0;
    CaptureRequest request;
    camera_control::Applied applied;
    uint32_t lastAttempt = 0;
    bool lastFailed = false;
    while (xQueueReceive(s_requests, &request, portMAX_DELAY) == pdTRUE) {
        // Outside capture_ms: the push happens once per change, not per frame.
        applyCameraSettings(request, applied, lastAttempt, lastFailed);
        CaptureReply reply;
        reply.epoch = request.epoch; reply.index = index;
        const uint32_t started = clockMs();
        reply.length = s_http.capture(request.url, s_jpeg_buffers[index], JPEG_CAPACITY, reply.error);
        reply.captureMs = clockMs() - started;
        lastFailed = reply.length == 0;
        xQueueSend(s_replies, &reply, portMAX_DELAY);
        index = 1 - index;
    }
}

class Plan2Runtime {
public:
    EdgeDeviceConfig cfg;
    EdgeFsm fsm;
    CaptureGate gate;
    LcdData data;
    LcdLines lines = {};
    bool servoOk = true;
    bool cameraTest = false;
    bool servoJog = false;   // USB "SERVO <deg>" calibration lease (servo_jog.h)
    float jogDeg = NAN;
    const char* testMessage = "Waiting frame";

    void begin() {
        cfg = configSnapshot(); fsm.setConfig(cfg); fsm.begin(clockMs());
    }
    void tick() {
        const auto next = configSnapshot();
        if (cfg.revision != next.revision) {
            servoDetach(); gate.invalidate();
            cfg = next; fsm.setConfig(cfg); fsm.reset(clockMs());
            held_ = {}; lastCommand_ = NAN; jogDeg = NAN;  // Jog re-attaches and re-commands.
        }
        float jogTarget = NAN;
        const bool requestedJog = servoJogTarget(jogTarget);
        if (requestedJog != servoJog) {
            // Enter/leave calibration: pause the FSM (no scale wake, no capture), release
            // the servo when leaving so driveServo() re-attaches with the sweep limits.
            servoDetach(); gate.invalidate(); fsm.reset(clockMs());
            held_ = {}; lastCommand_ = NAN; servoOk = true;
            servoJog = requestedJog; jogDeg = NAN;
        }
        if (servoJog) {
            setPowerMode(true);
            data = {};
            if (!servoAttached()) {
                servoAssumeAt(jogTarget);  // First attach after boot: snap only to the asked angle.
                servoOk = servoInit(cfg.servoPin, int(SWEEP_LIMIT_MIN_DEG), int(SWEEP_LIMIT_MAX_DEG));
            }
            if (servoOk && (!std::isfinite(jogDeg) || std::fabs(jogDeg - jogTarget) > .01f)) {
                servoMoveDeg(jogTarget, 0);
                jogDeg = jogTarget;
            }
            std::snprintf(lines.line[0], 17, "Servo jog %5.1f", jogTarget);
            std::snprintf(lines.line[1], 17, servoOk ? "Tilt %+5.1f deg" : "Servo error", servoToTiltDeg(jogTarget, cfg));
            lcdMessage(lines.line[0], lines.line[1]);
            return;
        }
        const bool requestedTest = cameraTestEnabled();
        if (requestedTest != cameraTest) {
            servoDetach(); gate.invalidate(); fsm.reset(clockMs());
            bleSetCameraTest(requestedTest);
            held_ = {}; lastCommand_ = NAN; servoOk = true;
            cameraTest = requestedTest; testMessage = "Waiting frame";
        }
        if (cameraTest) {
            // No setScale/update/driveServo: preview cannot wake or move the servo.
            setPowerMode(true);
            data = {};
            std::snprintf(lines.line[0], 17, "Camera test");
            std::snprintf(lines.line[1], 17, "%.16s", wifiReady() ? testMessage : "WiFi not ready");
            lcdMessage(lines.line[0], lines.line[1]);
            return;
        }
        const auto scale = scaleSnapshot();
        const uint32_t now = clockMs();
        const auto before = fsm.phase();
        fsm.setScale(now, scale.present, scale.kg, scale.stable);
        fsm.update(now);
        if (fsm.phase() == SystemPhase::WakingHoming && before != fsm.phase()) held_ = {};
        setPowerMode(fsm.phase() != SystemPhase::Standby);
        driveServo();
        LcdData live;
        live.hasHeight = fsm.hasHeight(); live.heightCm = fsm.heightCm();
        live.distanceM = fsm.distanceM(); live.hasWeight = scale.present;
        live.weightKg = scale.kg; live.weightStable = scale.present && scale.stable;
        live.hasBmi = live.hasHeight && live.weightStable;
        if (live.hasBmi) {
            const float metres = live.heightCm / 100;
            live.bmi = live.weightKg / (metres * metres);
        }
        if (fsm.phase() == SystemPhase::ResultHold) {
            if (before != SystemPhase::ResultHold) held_ = live;
            data = held_;
        } else data = live;
        const auto notice = !servoOk ? EdgeNotice::CameraError : fsm.notice();
        lines = lcdShowPhase(fsm.phase(), notice, data);
        if (!servoOk) lcdMessage("Servo error", "Check pin/config");
        if (lastPhase_ != fsm.phase()) {
            ESP_LOGI(TAG, "[SWEEP] -> %s servo=%.1f ms=%lu", phaseName(fsm.phase()),
                     fsm.servoCommand(), static_cast<unsigned long>(now));
            lastPhase_ = fsm.phase();
        }
    }
private:
    LcdData held_;
    SystemPhase lastPhase_ = SystemPhase::Standby;
    float lastCommand_ = NAN;
    void driveServo() {
        if (!fsm.servoActive()) {
            if (servoAttached()) { servoDetach(); gate.invalidate(); }
            lastCommand_ = NAN;
            return;
        }
        if (!servoAttached()) {
            const int lo = int(std::floor(std::min(cfg.bottomDeg, cfg.topDeg)));
            const int hi = int(std::ceil(std::max(cfg.bottomDeg, cfg.topDeg)));
            servoOk = servoInit(cfg.servoPin, lo, hi);
            gate.invalidate();
            if (!servoOk) return;
        }
        const float command = fsm.servoCommand();
        if (!std::isfinite(lastCommand_) || std::fabs(lastCommand_ - command) > .001f) {
            // Non-blocking (TV3 trapezoid in a 20 ms tick). servoSettled() = stopped for
            // servo_settle + servo_delay (camera image age), i.e. TV3 servoFrameFresh().
            gate.invalidate(); servoMoveDeg(command, cfg.settleMs + cfg.frameDelayMs);
            lastCommand_ = command;
        }
        servoUpdate();
    }
};

struct FrameTimings { uint32_t decode = 0, preprocess = 0, inference = 0, postprocess = 0; };

static void sendState(Plan2Runtime& rt, const uint8_t* jpeg = nullptr, size_t length = 0,
                      int width = 0, int height = 0, const std::vector<PersonBox>& boxes = {},
                      uint32_t processingMs = 0, uint32_t captureMs = 0,
                      float imageServo = NAN, float imageTilt = NAN, FrameTimings timing = {}) {
    std::string encoded = "[";
    for (size_t i = 0; i < boxes.size(); ++i) {
        char item[140]; const auto& b = boxes[i];
        std::snprintf(item, sizeof(item), "%s[%.1f,%.1f,%.1f,%.1f,%.3f]",
                      i ? "," : "", b.x1, b.y1, b.x2, b.y2, b.score);
        encoded += item;
    }
    encoded += "]";
    char h[32] = "null", d[32] = "null", w[32] = "null", bmi[32] = "null";
    char servo[32] = "null", tilt[32] = "null";
    if (rt.servoJog && std::isfinite(rt.jogDeg)) {
        snprintf(servo, sizeof(servo), "%.1f", rt.jogDeg);
        snprintf(tilt, sizeof(tilt), "%.1f", servoToTiltDeg(rt.jogDeg, rt.cfg));
    } else if (!rt.cameraTest) {
        snprintf(servo, sizeof(servo), "%.1f", std::isfinite(imageServo) ? imageServo : rt.fsm.servoCommand());
        snprintf(tilt, sizeof(tilt), "%.1f", std::isfinite(imageTilt) ? imageTilt : rt.fsm.tiltDeg());
    }
    if (rt.data.hasHeight) {
        snprintf(h, sizeof(h), "%.1f", rt.data.heightCm);
        snprintf(d, sizeof(d), "%.3f", rt.data.distanceM);
    }
    if (rt.data.hasWeight) snprintf(w, sizeof(w), "%.2f", rt.data.weightKg);
    if (rt.data.hasBmi) snprintf(bmi, sizeof(bmi), "%.2f", rt.data.bmi);
    char rssi[8] = "null";
    if (const int dbm = wifiRssi()) snprintf(rssi, sizeof(rssi), "%d", dbm);
    char metadata[3500];
    snprintf(metadata, sizeof(metadata),
        "{\"kind\":\"%s\",\"width\":%d,\"height\":%d,\"boxes\":%s,"
        "\"phase\":\"%s\",\"servo_deg\":%s,\"tilt_deg\":%s,\"framing\":\"%s\","
        "\"status\":\"%s\",\"message\":\"%s | %s\",\"margin_px\":%.1f,"
        "\"height_cm\":%s,\"distance_m\":%s,\"weight_kg\":%s,\"weight_stable\":%s,\"bmi\":%s,"
        "\"processing_ms\":%lu,\"capture_ms\":%lu,\"decode_ms\":%lu,\"preprocess_ms\":%lu,"
        "\"inference_ms\":%lu,\"postprocess_ms\":%lu,\"free_psram\":%u,\"preview\":%s,\"wifi_ready\":%s,"
        "\"wifi_rssi\":%s}",
        length ? "frame" : "status", width, height, encoded.c_str(),
        rt.servoJog ? "SERVO_JOG" : rt.cameraTest ? "CAMERA_TEST" : phaseName(rt.fsm.phase()), servo, tilt,
        rt.cameraTest ? "None" : framingName(rt.fsm.framing()),
        rt.cameraTest ? rt.testMessage : noticeName(rt.fsm.notice()),
        rt.lines.line[0], rt.lines.line[1], height * rt.cfg.marginFrac,
        h, d, w, rt.data.weightStable ? "true" : "false", bmi,
        static_cast<unsigned long>(processingMs), static_cast<unsigned long>(captureMs),
        static_cast<unsigned long>(timing.decode), static_cast<unsigned long>(timing.preprocess),
        static_cast<unsigned long>(timing.inference), static_cast<unsigned long>(timing.postprocess),
        unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
        rt.cameraTest ? "true" : "false", wifiReady() ? "true" : "false", rssi);
    usbFrame(jpeg, length, metadata);
}

extern "C" void app_main() {
    edgeIoInit();
    lcdMessage("Loading YOLO26n", "Please wait");
    // Copy model weights into 8-bit Octal PSRAM (80 MHz DDR) for faster
    // inference; fall back to Flash rodata only if PSRAM is tight.
    auto* model = new dl::Model(reinterpret_cast<const char*>(modelStart),
        fbs::MODEL_LOCATION_IN_FLASH_RODATA, 32 * 1024, dl::MEMORY_MANAGER_GREEDY, nullptr, true);
    if (!model->is_built()) {
        delete model;
        model = new dl::Model(reinterpret_cast<const char*>(modelStart),
            fbs::MODEL_LOCATION_IN_FLASH_RODATA, 0, dl::MEMORY_MANAGER_GREEDY, nullptr, true);
    }
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
    model->run(dl::RUNTIME_MODE_MULTI_CORE); // Exercise both cores before WiFi provisioning.
    char boot[192];
    snprintf(boot, sizeof(boot), "{\"kind\":\"boot\",\"self_test_ms\":%lu,\"free_psram\":%u,\"free_internal\":%u}",
        static_cast<unsigned long>(clockMs() - warmupAt), unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
        unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    usbFrame(nullptr, 0, boot);


    for (auto& buffer : s_jpeg_buffers)
        buffer = static_cast<uint8_t*>(heap_caps_malloc(JPEG_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    s_requests = xQueueCreate(1, sizeof(CaptureRequest));
    s_replies = xQueueCreate(1, sizeof(CaptureReply));
    if (!s_jpeg_buffers[0] || !s_jpeg_buffers[1] || !s_requests || !s_replies ||
        xTaskCreatePinnedToCore(captureWorkerTask, "cam_cap", 6144, nullptr, 4, nullptr, 0) != pdPASS) {
        lcdMessage("Capture RAM error", "Check PSRAM");
        while (true) { status("Capture task allocation failed"); vTaskDelay(pdMS_TO_TICKS(1000)); }
    }
    Plan2Runtime rt;
    rt.begin();
    uint32_t sentAt = clockMs();
    while (true) {
        rt.tick();
        CaptureReply reply;
        if (xQueueReceive(s_replies, &reply, 0) == pdTRUE) {
            if (rt.gate.current(reply.epoch) && reply.length &&
                (rt.cameraTest || rt.fsm.phase() == SystemPhase::ServoSweep || rt.fsm.phase() == SystemPhase::LockedMeasure)) {
                uint8_t* jpeg = s_jpeg_buffers[reply.index];
                const uint32_t inferAt = clockMs();
                auto img = detector.decode_jpeg(jpeg, reply.length);
                const uint32_t decodedAt = clockMs();
                if (img.data && img.width > 0 && img.height > 0 && img.width <= 1280 && img.height <= 1024) {
                    const int width = img.width, height = img.height;
                    detector.preprocess(img); heap_caps_free(img.data);
                    const uint32_t preprocessedAt = clockMs();
                    model->run(dl::RUNTIME_MODE_MULTI_CORE);
                    const uint32_t inferredAt = clockMs();
                    const auto detections = detector.postprocess(model->get_outputs());
                    const FrameTimings timing{decodedAt - inferAt, preprocessedAt - decodedAt,
                                              inferredAt - preprocessedAt, clockMs() - inferredAt};
                    std::vector<PersonBox> people;
                    for (const auto& r : detections) {
                        if (r.category == 0 && r.box.size() >= 4 && std::isfinite(r.score)) {
                            people.push_back(originalBox({float(r.box[0]), float(r.box[1]),
                                float(r.box[2]), float(r.box[3]), r.score}, width, height, EDGE_MODEL_INPUT));
                        }
                    }
                    rt.tick(); // Step-off or USB APPLY during inference invalidates this image.
                    if (rt.gate.current(reply.epoch)) {
                        BodyMessage message;
                        message.type = people.empty() ? BodyMessageType::NoBody :
                            people.size() > 1 ? BodyMessageType::Multi : BodyMessageType::Box;
                        if (people.size() == 1) {
                            const auto& b = people.front();
                            message.x1 = b.x1 / width; message.y1 = b.y1 / height;
                            message.x2 = b.x2 / width; message.y2 = b.y2 / height;
                            message.confidence = int(std::lround(b.score * 100));
                        }
                        const float imageServo = rt.fsm.servoCommand(), imageTilt = rt.fsm.tiltDeg();
                        if (rt.cameraTest) {
                            rt.testMessage = people.empty() ? "No person" :
                                people.size() == 1 ? "Person detected" : "Multiple people";
                            rt.tick();
                        } else rt.fsm.onFrame(clockMs(), message);
                        // Publish geometry at the angle used for this image BEFORE moving again.
                        sendState(rt, jpeg, reply.length, width, height, people,
                                  clockMs() - inferAt, reply.captureMs, imageServo, imageTilt, timing);
                        sentAt = clockMs();
                        rt.tick();
                    }
                } else if (img.data) heap_caps_free(img.data);
            } else if (rt.gate.current(reply.epoch) && !reply.length) {
                if (rt.cameraTest) rt.testMessage = "Camera error";
                ESP_LOGW(TAG, "Camera capture: %s", reply.error.detail);
                status(reply.error.detail);
            }
            rt.gate.finish(reply.epoch);
        }
        if (wifiReady() && rt.servoOk && rt.gate.canRequest(rt.fsm.phase(), servoSettled(), rt.cameraTest)) {
            CaptureRequest request;
            request.epoch = rt.gate.begin();
            snprintf(request.url, sizeof(request.url), "%s", rt.cfg.cameraUrl);
            request.framesize = rt.cfg.camFramesize; request.quality = rt.cfg.camQuality;
            if (xQueueSend(s_requests, &request, 0) != pdTRUE) rt.gate.finish(request.epoch);
        }
        if (clockMs() - sentAt >= 1000) { sendState(rt); sentAt = clockMs(); }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

