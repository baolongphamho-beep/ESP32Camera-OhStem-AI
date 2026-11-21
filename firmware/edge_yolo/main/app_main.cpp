#include "edge_io.h"
#include "edge_geometry.h"
#include "capture_gate.h"
#include "http_capture_parser.h"
#include "dl_model_base.hpp"
#include "yolo26.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <lwip/inet.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <sys/select.h>
#include <unistd.h>
#include <vector>

static const char* TAG = "edge_yolo";
extern const uint8_t modelStart[] asm("_binary_yolo26n_224_s8_s3_espdl_start");
static constexpr size_t JPEG_CAPACITY = 256 * 1024;

// Camera auto-resolution via OhStem Camera AI `/control?var=framesize&val=...`.
// Supported values: 3=HQVGA(240x176), 4=QVGA(320x240), 5=CIF(400x296),
// 6=VGA(640x480), 7=SVGA(800x600), 8=XGA(1024x768). Set -1 to disable.
static constexpr int kCameraTargetFramesize = 4; // QVGA (320x240)

static constexpr int cameraTargetMaxWidth(int framesize) {
    switch (framesize) {
        case 3: return 240;
        case 4: return 320;
        case 5: return 400;
        case 6: return 640;
        case 7: return 800;
        case 8: return 1024;
        default: return 1280;
    }
}

static void powerTelemetry(const char* phase, uint32_t generation, const MeasurementView* held = nullptr) {
    const auto scale = held ? *held : scaleSnapshot();
    const auto power = powerSnapshot();
    char json[768], weight[24] = "null", height[24] = "null", distance[24] = "null", bmi[24] = "null";
    if (scale.hasHeight) {
        snprintf(height, sizeof(height), "%.1f", scale.heightCm);
        snprintf(distance, sizeof(distance), "%.3f", scale.distanceM);
    }
    if (scale.hasBMI) snprintf(bmi, sizeof(bmi), "%.2f", scale.bmi);
    if (scale.hasWeight) snprintf(weight, sizeof(weight), "%.2f", scale.weightKg);
    snprintf(json, sizeof(json),
        "{\"kind\":\"scale\",\"phase\":\"%s\",\"session_id\":%lu,\"ble_duty_percent\":%.1f,"
        "\"weight_kg\":%s,\"weight_stable\":%s,\"pm_enabled\":%s,\"cpu_mhz\":%d,"
        "\"cpu_min_mhz\":%d,\"cpu_max_mhz\":%d,\"light_sleep_enabled\":%s,"
        "\"usb_sleep_blocked\":%s,\"no_ls_locks\":%d,\"wifi_ps\":\"%s\",\"wifi_started\":%s,"
        "\"height_cm\":%s,\"distance_m\":%s,\"bmi\":%s}",
        phase, static_cast<unsigned long>(generation), bleScanDutyPercent(), weight,
        scale.weightStable ? "true" : "false", power.pmEnabled ? "true" : "false", power.cpuMhz,
        power.minCpuMhz, power.maxCpuMhz, power.lightSleepConfigured ? "true" : "false",
        power.usbSleepBlocked ? "true" : "false", power.noLightSleepLocks, power.wifiPowerSave,
        power.wifiStarted ? "true" : "false", height, distance, bmi);
    usbFrame(nullptr, 0, json);
}

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

class HttpCaptureClient {
public:
    static constexpr uint32_t kConnectTimeoutMs = 2500;
    static constexpr uint32_t kHeaderTimeoutMs = 4500;
    static constexpr uint32_t kBodyIdleTimeoutMs = 3000;
    static constexpr uint32_t kTotalDeadlineMs = 6000;
    static constexpr uint32_t kFinDrainMs = 40;
    static constexpr size_t kMaxHeaderBytes = 4096;

    ~HttpCaptureClient() { reset(); }

    void reset() {
        closeSocket();
        url_[0] = '\0';
        url_valid_ = false;
        addr_valid_ = false;
    }

    bool applyCameraFramesize(const char* url, int framesize) {
        if (framesize < 0) return false;
        CaptureError err;
        if (!ensureUrlResolved(url, err)) {
            return false;
        }
        const uint32_t totalDeadline = clockMs() + kConnectTimeoutMs;
        if (!openSocket(totalDeadline, err)) {
            return false;
        }
        struct SocketCloser {
            HttpCaptureClient* self;
            ~SocketCloser() { self->closeSocket(); }
        } socketCloser{this};

        char req[256];
        const int reqLen = snprintf(req, sizeof(req),
            "GET /control?var=framesize&val=%d HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Connection: close\r\n"
            "User-Agent: SmartScale/1\r\n\r\n",
            framesize, parsed_.hostHeader);
        if (reqLen <= 0 || static_cast<size_t>(reqLen) >= sizeof(req)) {
            return false;
        }

        int ioErr = 0;
        if (!sendAll(reinterpret_cast<const uint8_t*>(req), static_cast<size_t>(reqLen), totalDeadline, ioErr)) {
            return false;
        }

        size_t hdrLen = 0;
        HttpResponseHead head;
        HeaderParseStatus hStatus = HeaderParseStatus::Incomplete;
        while (hdrLen < kMaxHeaderBytes) {
            const ssize_t n = recvSome(rx_buf_ + hdrLen, kMaxHeaderBytes - hdrLen, totalDeadline, ioErr);
            if (n <= 0) {
                break;
            }
            hdrLen += static_cast<size_t>(n);
            hStatus = parseHttpResponseHead(rx_buf_, hdrLen, head);
            if (hStatus != HeaderParseStatus::Incomplete) {
                break;
            }
        }
        if (hStatus == HeaderParseStatus::Complete && head.statusCode == 200) {
            drainPeerFin(kFinDrainMs);
            return true;
        }
        return false;
    }

    size_t capture(const char* url, uint8_t* buffer, size_t capacity, CaptureError& err) {
        if (!ensureUrlResolved(url, err)) {
            return 0;
        }

        const uint32_t startMs = clockMs();
        const uint32_t totalDeadline = startMs + kTotalDeadlineMs;

        if (!openSocket(totalDeadline, err)) {
            return 0;
        }
        struct SocketCloser {
            HttpCaptureClient* self;
            ~SocketCloser() { self->closeSocket(); }
        } socketCloser{this};

        char req[384];
        const int reqLen = snprintf(req, sizeof(req),
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Connection: close\r\n"
            "User-Agent: SmartScale/1\r\n\r\n",
            parsed_.path, parsed_.hostHeader);
        if (reqLen <= 0 || static_cast<size_t>(reqLen) >= sizeof(req)) {
            captureFail(err, "Bad camera URL", "Camera URL path too long");
            return 0;
        }

        int ioErr = 0;
        if (!sendAll(reinterpret_cast<const uint8_t*>(req), static_cast<size_t>(reqLen), totalDeadline, ioErr)) {
            captureFail(err, "Cam: send fail", "Failed to send HTTP request (errno=%d)", ioErr);
            return 0;
        }

        const uint32_t hdrDeadline = earlierDeadline(totalDeadline, startMs + kHeaderTimeoutMs);
        size_t hdrLen = 0;
        HttpResponseHead head;
        HeaderParseStatus hStatus = HeaderParseStatus::Incomplete;
        while (hdrLen < kMaxHeaderBytes) {
            const ssize_t n = recvSome(rx_buf_ + hdrLen, kMaxHeaderBytes - hdrLen, hdrDeadline, ioErr);
            if (n > 0) {
                hdrLen += static_cast<size_t>(n);
                hStatus = parseHttpResponseHead(rx_buf_, hdrLen, head);
                if (hStatus != HeaderParseStatus::Incomplete) {
                    break;
                }
            } else if (n == 0) {
                captureFail(err, "Cam: no reply", "Camera closed the connection without an HTTP reply");
                return 0;
            } else {
                if (ioErr == ETIMEDOUT) {
                    captureFail(err, "Cam: no reply",
                        "Camera accepted TCP but sent no HTTP reply in 4.5 s (busy/weak Wi-Fi?)");
                } else {
                    captureFail(err, "Cam: no reply", "Camera header read failed (errno=%d)", ioErr);
                }
                return 0;
            }
        }
        if (hStatus == HeaderParseStatus::Invalid ||
            (hStatus == HeaderParseStatus::Incomplete && hdrLen >= kMaxHeaderBytes)) {
            captureFail(err, "Cam: bad HTTP", "Malformed HTTP header from camera");
            return 0;
        }
        if (head.statusCode != 200) {
            captureFail(err, "Cam: HTTP error", "Camera replied HTTP %d (is /capture the right path?)", head.statusCode);
            return 0;
        }
        if (head.hasContentLength && head.contentLength > capacity) {
            captureFail(err, "Cam: image big", "Image %u B > %u B: use QVGA/VGA",
                        unsigned(head.contentLength), unsigned(capacity));
            return 0;
        }

        size_t used = 0;
        bool completeBody = false;
        const uint8_t* leftover = rx_buf_ + head.headerBytes;
        const size_t leftoverLen = hdrLen - head.headerBytes;

        if (head.isChunked) {
            ChunkedBodyDecoder decoder;
            bool ok = decoder.feed(leftover, leftoverLen, buffer, capacity, used);
            while (ok && !decoder.done()) {
                const uint32_t readDeadline = earlierDeadline(totalDeadline, clockMs() + kBodyIdleTimeoutMs);
                const ssize_t n = recvSome(rx_buf_, sizeof(rx_buf_), readDeadline, ioErr);
                if (n <= 0) break;
                ok = decoder.feed(rx_buf_, static_cast<size_t>(n), buffer, capacity, used);
            }
            if (decoder.overflow()) {
                captureFail(err, "Cam: image big", "Image > %u B: use QVGA/VGA", unsigned(capacity));
                return 0;
            }
            completeBody = (ok && decoder.done());
            if (completeBody) {
                drainPeerFin(kFinDrainMs);
            }
        } else if (head.hasContentLength) {
            const size_t take = std::min(leftoverLen, head.contentLength);
            if (take > 0) {
                memcpy(buffer, leftover, take);
                used = take;
            }
            while (used < head.contentLength) {
                const uint32_t readDeadline = earlierDeadline(totalDeadline, clockMs() + kBodyIdleTimeoutMs);
                const ssize_t n = recvSome(buffer + used, head.contentLength - used, readDeadline, ioErr);
                if (n <= 0) break;
                used += static_cast<size_t>(n);
            }
            completeBody = (used == head.contentLength);
            if (completeBody) {
                drainPeerFin(kFinDrainMs);
            }
        } else {
            const size_t take = std::min(leftoverLen, capacity);
            if (take > 0) {
                memcpy(buffer, leftover, take);
                used = take;
            }
            while (used < capacity) {
                if (used >= 4 && buffer[0] == 0xff && buffer[1] == 0xd8 &&
                    buffer[used - 2] == 0xff && buffer[used - 1] == 0xd9) {
                    completeBody = true;
                    drainPeerFin(kFinDrainMs);
                    break;
                }
                const uint32_t readDeadline = earlierDeadline(totalDeadline, clockMs() + kBodyIdleTimeoutMs);
                const ssize_t n = recvSome(buffer + used, std::min<size_t>(8192, capacity - used), readDeadline, ioErr);
                if (n == 0) {
                    completeBody = true;
                    break;
                }
                if (n < 0) {
                    break;
                }
                used += static_cast<size_t>(n);
            }
        }

        const size_t validLen = trimAndValidateJpeg(buffer, used);
        if (validLen > 0) {
            return validLen;
        }
        if (!completeBody) {
            captureFail(err, "Cam: cut image", "Image transfer incomplete after %u B (timeout/weak Wi-Fi)",
                        unsigned(used));
            return 0;
        }
        char headStr[25] = {};
        const size_t shown = std::min(used, sizeof(headStr) - 1);
        for (size_t i = 0; i < shown; ++i) {
            headStr[i] = (buffer[i] >= 0x20 && buffer[i] < 0x7f) ? char(buffer[i]) : '.';
        }
        // e.g. {"busy":1} when a browser tab/other script is using the camera.
        captureFail(err, "Cam: busy/notJPG",
                    "Reply is not a JPEG (%u B, starts '%s'): close other camera clients",
                    unsigned(used), headStr);
        return 0;
    }

private:
    static int32_t remainMs(uint32_t deadline) {
        return static_cast<int32_t>(deadline - clockMs());
    }

    static uint32_t earlierDeadline(uint32_t a, uint32_t b) {
        return (static_cast<int32_t>(a - b) <= 0) ? a : b;
    }

    void closeSocket() {
        if (sock_ >= 0) {
            close(sock_);
            sock_ = -1;
        }
    }

    void drainPeerFin(uint32_t maxMs) {
        if (sock_ < 0 || maxMs == 0) return;
        const uint32_t deadline = clockMs() + maxMs;
        uint8_t sink[64];
        while (true) {
            const ssize_t n = recv(sock_, sink, sizeof(sink), 0);
            if (n == 0) {
                break; // Peer FIN arrived; UNO performs passive close cleanly.
            }
            if (n > 0) {
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                break;
            }
            const int32_t rem = remainMs(deadline);
            if (rem <= 0) break;
            fd_set rset, eset;
            FD_ZERO(&rset);
            FD_ZERO(&eset);
            FD_SET(sock_, &rset);
            FD_SET(sock_, &eset);
            struct timeval tv = { rem / 1000, (rem % 1000) * 1000 };
            if (select(sock_ + 1, &rset, nullptr, &eset, &tv) <= 0) {
                break;
            }
        }
    }

    bool ensureUrlResolved(const char* url, CaptureError& err) {
        if (!url || !url[0]) {
            captureFail(err, "Bad camera URL", "Empty camera URL");
            return false;
        }
        if (!url_valid_ || strcmp(url_, url) != 0) {
            reset();
            if (strlen(url) >= sizeof(url_) || !parseHttpUrl(url, parsed_)) {
                captureFail(err, "Bad camera URL", "Invalid camera URL '%s'", url);
                return false;
            }
            strncpy(url_, url, sizeof(url_) - 1);
            url_[sizeof(url_) - 1] = '\0';
            url_valid_ = true;
            addr_valid_ = false;
        }
        if (!addr_valid_ && !resolveAddress(err)) {
            return false;
        }
        return true;
    }

    bool resolveAddress(CaptureError& err) {
        memset(&addr_, 0, sizeof(addr_));
        addr_.sin_family = AF_INET;
        addr_.sin_port = htons(parsed_.port);
        if (inet_pton(AF_INET, parsed_.host, &addr_.sin_addr) == 1) {
            addr_valid_ = true;
            return true;
        }
        struct addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo* res = nullptr;
        const int rc = getaddrinfo(parsed_.host, nullptr, &hints, &res);
        if (rc != 0 || !res) {
            captureFail(err, "Cam: bad host", "Cannot resolve camera host '%s' (%d)", parsed_.host, rc);
            return false;
        }
        const auto* resolved = reinterpret_cast<const struct sockaddr_in*>(res->ai_addr);
        addr_.sin_addr = resolved->sin_addr;
        freeaddrinfo(res);
        addr_valid_ = true;
        return true;
    }

    bool openSocket(uint32_t deadline, CaptureError& err) {
        const int32_t rem = remainMs(deadline);
        if (rem <= 0) {
            captureFail(err, "Cam: no connect", "Camera connect timed out");
            return false;
        }

        const int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s < 0) {
            captureFail(err, "Cam: no connect", "Cannot create TCP socket (errno=%d)", errno);
            return false;
        }

        const int one = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        const int flags = fcntl(s, F_GETFL, 0);
        if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0) {
            const int errCode = errno;
            close(s);
            captureFail(err, "Cam: no connect", "Cannot set nonblocking socket (errno=%d)", errCode);
            return false;
        }

        const int connRc = connect(s, reinterpret_cast<const struct sockaddr*>(&addr_), sizeof(addr_));
        if (connRc < 0) {
            if (errno != EINPROGRESS) {
                const int errCode = errno;
                close(s);
                captureFail(err, "Cam: no connect", "Cannot connect to camera (errno=%d)", errCode);
                return false;
            }
            const int32_t waitMs = std::min<int32_t>(static_cast<int32_t>(kConnectTimeoutMs), rem);
            fd_set wset, eset;
            FD_ZERO(&wset);
            FD_ZERO(&eset);
            FD_SET(s, &wset);
            FD_SET(s, &eset);
            struct timeval tv = { waitMs / 1000, (waitMs % 1000) * 1000 };
            const int sel = select(s + 1, nullptr, &wset, &eset, &tv);
            if (sel <= 0) {
                const int errCode = (sel == 0) ? ETIMEDOUT : errno;
                close(s);
                captureFail(err, "Cam: no connect", "Cannot connect to camera (errno=%d)", errCode);
                return false;
            }
            int soErr = 0;
            socklen_t optLen = sizeof(soErr);
            if (getsockopt(s, SOL_SOCKET, SO_ERROR, &soErr, &optLen) < 0 || soErr != 0) {
                const int errCode = (soErr != 0) ? soErr : errno;
                close(s);
                captureFail(err, "Cam: no connect", "Cannot connect to camera (errno=%d)", errCode);
                return false;
            }
        }

        sock_ = s;
        return true;
    }

    bool sendAll(const uint8_t* data, size_t len, uint32_t deadline, int& outErr) {
        outErr = 0;
        size_t sent = 0;
        while (sent < len) {
            const ssize_t n = send(sock_, data + sent, len - sent, 0);
            if (n > 0) {
                sent += static_cast<size_t>(n);
                continue;
            }
            if (n == 0) {
                outErr = ECONNRESET;
                return false;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                outErr = errno;
                return false;
            }
            const int32_t rem = remainMs(deadline);
            if (rem <= 0) {
                outErr = ETIMEDOUT;
                return false;
            }
            fd_set wset, eset;
            FD_ZERO(&wset);
            FD_ZERO(&eset);
            FD_SET(sock_, &wset);
            FD_SET(sock_, &eset);
            struct timeval tv = { rem / 1000, (rem % 1000) * 1000 };
            const int sel = select(sock_ + 1, nullptr, &wset, &eset, &tv);
            if (sel == 0) {
                outErr = ETIMEDOUT;
                return false;
            }
            if (sel < 0) {
                outErr = errno;
                return false;
            }
            if (FD_ISSET(sock_, &eset)) {
                int soErr = 0;
                socklen_t optLen = sizeof(soErr);
                getsockopt(sock_, SOL_SOCKET, SO_ERROR, &soErr, &optLen);
                outErr = (soErr != 0) ? soErr : ECONNRESET;
                return false;
            }
        }
        return true;
    }

    ssize_t recvSome(uint8_t* dst, size_t maxLen, uint32_t deadline, int& outErr) {
        outErr = 0;
        while (true) {
            const ssize_t n = recv(sock_, dst, maxLen, 0);
            if (n >= 0) {
                return n;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                outErr = errno;
                return -1;
            }
            const int32_t rem = remainMs(deadline);
            if (rem <= 0) {
                outErr = ETIMEDOUT;
                return -1;
            }
            fd_set rset, eset;
            FD_ZERO(&rset);
            FD_ZERO(&eset);
            FD_SET(sock_, &rset);
            FD_SET(sock_, &eset);
            struct timeval tv = { rem / 1000, (rem % 1000) * 1000 };
            const int sel = select(sock_ + 1, &rset, nullptr, &eset, &tv);
            if (sel == 0) {
                outErr = ETIMEDOUT;
                return -1;
            }
            if (sel < 0) {
                outErr = errno;
                return -1;
            }
        }
    }

    int sock_ = -1;
    char url_[160] = {};
    ParsedHttpUrl parsed_{};
    bool url_valid_ = false;
    struct sockaddr_in addr_{};
    bool addr_valid_ = false;
    uint8_t rx_buf_[kMaxHeaderBytes] = {};
};

// Double-buffered background capture state so Core 0 downloads frame N+1 over
// Wi-Fi while Core 1 decodes and runs YOLO26n inference on frame N.
static uint8_t* s_jpeg_buffers[2] = {nullptr, nullptr};
static SemaphoreHandle_t s_capture_mutex = nullptr;
static SemaphoreHandle_t s_capture_sem = nullptr;
static SemaphoreHandle_t s_capture_wake_sem = nullptr;
static int s_ready_idx = -1;
static int s_busy_idx = -1;
static size_t s_ready_len = 0;
static uint32_t s_ready_capture_ms = 0;
static uint32_t s_consecutive_failures = 0;
static CaptureError s_last_error;
static CaptureGate s_gate;
static uint32_t s_ready_generation = 0;
static uint32_t s_requested_scale_session = 0;
static MeasurementView s_last_result;
static bool s_have_result = false;
static std::atomic<const char*> s_system_phase{"STANDBY"};
static std::atomic<bool> s_camera_res_dirty{true};

struct InferenceLease {
    uint32_t token;
    ~InferenceLease() {
        xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
        s_busy_idx = -1;
        s_gate.finishInference(token);
        xSemaphoreGive(s_capture_mutex);
    }
};

static void requestCapture(bool active, uint32_t scaleSession) {
    bool woke = false;
    xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
    if (s_gate.request(active, scaleSession)) {
        s_ready_idx = -1;
        s_consecutive_failures = 0;
        while (xSemaphoreTake(s_capture_sem, 0) == pdTRUE) {}
        woke = active;
    }
    s_requested_scale_session = scaleSession;
    xSemaphoreGive(s_capture_mutex);
    if (woke && s_capture_wake_sem) {
        xSemaphoreGive(s_capture_wake_sem);
    }
}

static void integrationControlTask(void* arg) {
    bool requested = false, holding = false;
    uint32_t session = 0, holdAt = 0, lastReportAt = 0, lastGeneration = UINT32_MAX;
    const char* lastPhase = nullptr;
    while (true) {
#ifdef EDGE_CAMERA_CONTINUOUS
        const bool active = true;
        const uint32_t wantedSession = 0;
#else
        const auto sample = scaleSampleSnapshot();
        const bool active = sample.present();
        const uint32_t wantedSession = sample.sessionId;
#endif
        if (active && (!requested || wantedSession != session)) {
            lcdHoldResult(nullptr);
            setPowerMode(true);
            xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
            s_have_result = false;
            xSemaphoreGive(s_capture_mutex);
            requestCapture(true, wantedSession);
            requested = true;
            session = wantedSession;
            holding = false;
#ifdef EDGE_CAMERA_CONTINUOUS
            s_system_phase.store("CONTINUOUS");
#else
            s_system_phase.store("ACTIVE");
#endif
        } else if (!active && requested) {
            requestCapture(false, session);
            requested = false;
            s_system_phase.store("DRAINING");
        }
        if (!active) {
            xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
            const bool idle = s_gate.canSleep();
            const bool hasResult = s_have_result;
            const auto result = s_last_result;
            xSemaphoreGive(s_capture_mutex);
            // Both workers must release their buffers/HTTP handles before DFS.
            if (idle && powerModeActive()) {
                setPowerMode(false);
                cameraState(PersonStatus::None);
                if (hasResult) {
                    lcdHoldResult(&result);
                    lcdSetBacklight(true);
                    holding = true;
                    holdAt = clockMs();
                    s_system_phase.store("RESULT_HOLD");
                } else {
                    s_system_phase.store("STANDBY");
                }
            }
            if (holding && clockMs() - holdAt >= 10000) {
                lcdHoldResult(nullptr);
                lcdSetBacklight(true);
                holding = false;
                s_system_phase.store("STANDBY");
            }
        }
        xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
        const uint32_t generation = s_gate.generation();
        const auto heldResult = s_last_result;
        xSemaphoreGive(s_capture_mutex);
        const char* phase = s_system_phase.load();
        const uint32_t now = clockMs();
        if (phase != lastPhase || generation != lastGeneration || now - lastReportAt >= 1000) {
            powerTelemetry(phase, generation, holding ? &heldResult : nullptr);
            lastReportAt = now;
            lastPhase = phase;
            lastGeneration = generation;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void captureWorkerTask(void* arg) {
    static HttpCaptureClient httpClient;
    static uint32_t last_res_attempt_ms = 0;
    static char last_res_url[160] = {};
    while (true) {
        if (!wifiReady()) {
            httpClient.reset();
            s_camera_res_dirty.store(true);
            last_res_attempt_ms = 0;
            xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
            s_ready_idx = -1;
            s_ready_len = 0;
            s_consecutive_failures = 0;
            xSemaphoreGive(s_capture_mutex);
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        int write_idx = -1;
        uint32_t token = 0;
        xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
        const bool enabled = s_gate.enabled();
        if (s_ready_idx == -1 && s_gate.beginCapture(token)) {
            write_idx = (s_busy_idx == 0) ? 1 : 0;
        }
        xSemaphoreGive(s_capture_mutex);

        if (write_idx < 0) {
            if (!enabled) {
                httpClient.reset();
            }
            // Wait on wake semaphore so stepping on the scale triggers HTTP
            // capture immediately (0 ms) instead of sleeping through a poll tick.
            xSemaphoreTake(s_capture_wake_sem, pdMS_TO_TICKS(enabled ? 15 : 100));
            continue;
        }

        const auto cfg = configSnapshot();
        if (kCameraTargetFramesize >= 0) {
            if (strcmp(last_res_url, cfg.cameraUrl) != 0) {
                strncpy(last_res_url, cfg.cameraUrl, sizeof(last_res_url) - 1);
                last_res_url[sizeof(last_res_url) - 1] = '\0';
                s_camera_res_dirty.store(true);
                last_res_attempt_ms = 0;
            }
            const uint32_t nowMs = clockMs();
            if (s_camera_res_dirty.load() &&
                (last_res_attempt_ms == 0 || nowMs - last_res_attempt_ms >= 10000)) {
                last_res_attempt_ms = nowMs;
                const bool applied = httpClient.applyCameraFramesize(cfg.cameraUrl, kCameraTargetFramesize);
                s_camera_res_dirty.store(false);
                if (applied) {
                    ESP_LOGI(TAG, "Camera framesize set to %d (max %d px)",
                             kCameraTargetFramesize, cameraTargetMaxWidth(kCameraTargetFramesize));
                    vTaskDelay(pdMS_TO_TICKS(120));
                }
            }
        }
        const auto started = clockMs();
        CaptureError err;
        const size_t length = httpClient.capture(cfg.cameraUrl, s_jpeg_buffers[write_idx], JPEG_CAPACITY, err);
        const uint32_t elapsed = clockMs() - started;

        xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
        const bool accepted = s_gate.finishCapture(token);
        if (!accepted) {
            xSemaphoreGive(s_capture_mutex);
            continue;
        }
        if (length > 0) {
            s_ready_generation = token;
            s_ready_idx = write_idx;
            s_ready_len = length;
            s_ready_capture_ms = elapsed;
            s_consecutive_failures = 0;
            xSemaphoreGive(s_capture_mutex);
            xSemaphoreGive(s_capture_sem);
            // Adaptive breather: hidden behind Core 1 inference when fast,
            // backs off up to 900 ms when OhStem/Wi-Fi is congested.
            const uint32_t breather_ms = std::clamp<uint32_t>(elapsed / 2, 150, 900);
            vTaskDelay(pdMS_TO_TICKS(breather_ms));
        } else {
            ++s_consecutive_failures;
            s_last_error = err;
            const uint32_t fails = s_consecutive_failures;
            xSemaphoreGive(s_capture_mutex);
            if (fails >= 3) {
                xSemaphoreGive(s_capture_sem);
            }
            // Gentle retry backoff so a busy camera is not dogpiled.
            const uint32_t retry_ms = (fails == 1) ? 400 : (fails == 2) ? 1000 :
                                      std::min<uint32_t>(3500, 1200 + fails * 500);
            vTaskDelay(pdMS_TO_TICKS(retry_ms));
        }
    }
}

extern "C" void app_main() {
    edgeIoInit();
#ifdef EDGE_SCALE_ONLY
    setPowerMode(false);
    uint32_t lastReportAt = 0;
    bool firstReport = true;
    while (true) {
        const auto scale = scaleSnapshot();
        const bool active = scale.hasWeight && scale.weightKg > 5.0f;
        const bool changed = active != powerModeActive();
        if (changed) setPowerMode(active);
        const uint32_t now = clockMs();
        if (firstReport || changed || now - lastReportAt >= 1000) {
            powerTelemetry(active ? "ACTIVE" : "STANDBY", scaleSampleSnapshot().sessionId);
            firstReport = false;
            lastReportAt = now;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
#endif
    lcdMessage("Loading YOLO26n", "Please wait");

    // Copy model weights into 8-bit Octal PSRAM (80 MHz DDR) while keeping
    // internal DMA SRAM free for Wi-Fi, lwIP TCP/IP, HTTP, and task stacks.
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
    ESP_LOGI(TAG, "dl::Model built (free_int=%u, largest_int=%u)",
             unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));

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
    char boot[224];
    snprintf(boot, sizeof(boot),
        "{\"kind\":\"boot\",\"self_test_ms\":%lu,\"free_psram\":%u,\"free_internal\":%u,\"largest_internal\":%u}",
        static_cast<unsigned long>(clockMs() - warmupAt),
        unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
        unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    usbFrame(nullptr, 0, boot);

    s_jpeg_buffers[0] = static_cast<uint8_t*>(heap_caps_malloc(JPEG_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    s_jpeg_buffers[1] = static_cast<uint8_t*>(heap_caps_malloc(JPEG_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!s_jpeg_buffers[0] || !s_jpeg_buffers[1]) {
        lcdMessage("JPEG RAM error", "Check PSRAM");
        while (true) { status("JPEG allocation failed"); vTaskDelay(pdMS_TO_TICKS(1000)); }
    }
    s_capture_mutex = xSemaphoreCreateMutex();
    s_capture_sem = xSemaphoreCreateBinary();
    s_capture_wake_sem = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(s_capture_mutex && s_capture_sem && s_capture_wake_sem ? ESP_OK : ESP_ERR_NO_MEM);
    // Pin background HTTP capture to Core 0 (alongside Wi-Fi/lwIP) so Core 1
    // (app_main affinity) runs YOLO26n inference uninterrupted in parallel.
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(captureWorkerTask, "cam_cap", 8192, nullptr, 4, nullptr, 0)
                    == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(integrationControlTask, "integration", 4096, nullptr, 3, nullptr, 0)
                    == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_LOGI(TAG, "YOLO26n ready; free PSRAM=%u", unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));

    uint32_t lastConfigRev = configSnapshot().revision;
    uint32_t lastReportedFails = 0;
    HeightMedianFilter heightFilter;
    AutoCalibCollector calibCollector;
    int consecutiveNone = 0;
    bool hadPerson = false;
    static constexpr int kNoneDebounceFrames = 3;

    while (true) {
        xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
        const bool enabled = s_gate.enabled();
        xSemaphoreGive(s_capture_mutex);
        if (!enabled) {
            lastReportedFails = 0;
            heightFilter.reset();
            calibCollector.reset();
            consecutiveNone = 0;
            hadPerson = false;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (!wifiReady()) {
            lastReportedFails = 0;
            heightFilter.reset();
            calibCollector.reset();
            consecutiveNone = 0;
            hadPerson = false;
            clearMeasurementHold();
            cameraState(PersonStatus::None);
            lcdMessage("WiFi not ready", "Setup via USB");
            status("WiFi not ready: configure via USB");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        xSemaphoreTake(s_capture_sem, pdMS_TO_TICKS(100));

        int buf_idx = -1;
        uint32_t token = 0;
        size_t length = 0;
        uint32_t captureMs = 0;
        uint32_t fails = 0;
        CaptureError err;

        xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
        if (s_ready_idx >= 0 && s_gate.beginInference(s_ready_generation)) {
            token = s_ready_generation;
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
        if (cfg.revision != lastConfigRev) {
            lastConfigRev = cfg.revision;
            heightFilter.reset();
            calibCollector.reset();
            consecutiveNone = 0;
            hadPerson = false;
        }

        if (buf_idx < 0 || !length) {
            // Only report offline after 3+ consecutive failures so brief 1-2
            // packet Wi-Fi hiccups do not interrupt the live display or LCD.
            if (fails >= 3) {
                heightFilter.reset();
                calibCollector.reset();
                consecutiveNone = 0;
                hadPerson = false;
                clearMeasurementHold();
                char ip[16];
                wifiIp(ip, sizeof(ip));
                cameraState(PersonStatus::None);
                if (fails % 2) lcdMessage("Camera offline", err.lcd);
                else lcdMessage("UNO IP:", ip);
                if (fails != lastReportedFails) {
                    lastReportedFails = fails;
                    char text[420];
                    snprintf(text, sizeof(text),
                             "Camera capture failed: %s | URL=%s | UNO IP=%s | fails=%lu rssi=%d free_int=%u",
                             err.detail, cfg.cameraUrl, ip,
                             static_cast<unsigned long>(fails),
                             wifiRssi(),
                             unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
                    status(text);
                    ESP_LOGW(TAG, "%s", text);
                }
            }
            continue;
        }
        lastReportedFails = 0;

        InferenceLease lease{token};
        uint8_t* jpeg = s_jpeg_buffers[buf_idx];
        const auto inferStarted = clockMs();
        auto img = detector.decode_jpeg(jpeg, length);
        if (!img.data) {
            continue;
        }
        const int width = img.width, height = img.height;
        if (kCameraTargetFramesize >= 0 && width > cameraTargetMaxWidth(kCameraTargetFramesize)) {
            s_camera_res_dirty.store(true);
        }
        if (width <= 0 || height <= 0 || width > 1280 || height > 1024) {
            heap_caps_free(img.data);
            heightFilter.reset();
            calibCollector.reset();
            consecutiveNone = 0;
            hadPerson = false;
            cameraState(PersonStatus::None);
            status("Use QVGA or VGA camera resolution");
            continue;
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
        bool updateCameraState = true;

        if (people.empty()) {
            if (hadPerson && ++consecutiveNone < kNoneDebounceFrames) {
                // Tolerate 1-2 missed detections without clearing active scale weight/BMI.
                label = "No people detected";
                updateCameraState = false;
            } else {
                hadPerson = false;
                consecutiveNone = 0;
                heightFilter.reset();
                calibCollector.reset();
            }
        } else if (people.size() > 1) {
            consecutiveNone = 0;
            hadPerson = false;
            heightFilter.reset();
            calibCollector.reset();
            person = PersonStatus::Multiple;
            label = "Multiple people";
        } else {
            consecutiveNone = 0;
            hadPerson = true;
            const auto& box = people.front();
            person = PersonStatus::Partial;
            label = "Show head and feet";
            if (!fullBox(box, width, height)) {
                calibCollector.reset();
            } else {
                person = PersonStatus::NeedsCalibration;
                label = "Check calibration";
                // Store focal in normalized image-height units, so a resolution
                // change does not silently invalidate it (viewer calls it fy/H).
                float focal = cfg.focalPx * height;
                if (!cfg.groundMode && focal == 0 && std::fabs(cfg.tiltDeg) < .01f) {
                    if (cfg.distanceM > 0 && cfg.referenceCm > 0) {
                        float medBoxH = 0;
                        if (calibCollector.add(box.y2 - box.y1, medBoxH)) {
                            const float calibratedFocal = medBoxH * cfg.distanceM / (cfg.referenceCm / 100.0f);
                            if (std::isfinite(calibratedFocal) && calibratedFocal > 0.0f) {
                                focal = calibratedFocal;
                                saveFocalIfUnchanged(cfg, focal / height);
                            }
                        } else {
                            label = "Hold still to calibrate";
                        }
                    }
                } else {
                    calibCollector.reset();
                }

                bool valid = false;
                if (cfg.groundMode) {
                    valid = groundHeight(box, width, height, focal,
                        cfg.cameraHeightCm, cfg.tiltDeg, cm, distance);
                } else if (focal > 0 && std::fabs(cfg.tiltDeg) < .01f) {
                    distance = cfg.distanceM;
                    cm = 100 * (box.y2 - box.y1) * distance / focal;
                    valid = std::isfinite(cm) && cm >= 30 && cm <= 250 && distance > 0 && distance <= 20;
                }
                if (valid) {
                    person = PersonStatus::Measured;
                    label = "Full body detected";
                    heightFilter.push(cm, distance);
                    heightFilter.median(cm, distance);
                }
            }
        }

        xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
        bool publish = s_gate.accepts(token);
#ifndef EDGE_CAMERA_CONTINUOUS
        const auto currentScale = scaleSampleSnapshot();
        publish = publish && currentScale.present() &&
                  currentScale.sessionId == s_requested_scale_session;
#endif
        if (!publish) {
            xSemaphoreGive(s_capture_mutex);
            continue;
        }
        MeasurementView view;
#ifdef EDGE_CAMERA_CONTINUOUS
        if (updateCameraState) {
            cameraState(person, cm, distance);
        }
        view = stateSnapshot();
#else
        if (updateCameraState) {
            if (!cameraStateForScaleSession(s_requested_scale_session, person, cm, distance, view)) {
                xSemaphoreGive(s_capture_mutex);
                continue;
            }
        } else {
            view = stateSnapshot();
        }
#endif
        if (view.hasBMI) {
            s_last_result = view;
            s_have_result = true;
        } else if (person == PersonStatus::Multiple) {
            s_have_result = false;
        }
        xSemaphoreGive(s_capture_mutex);

        char boxes[1600];
        size_t boxPos = 0;
        boxes[boxPos++] = '[';
        const size_t maxBoxes = std::min<size_t>(people.size(), 20);
        for (size_t i = 0; i < maxBoxes && boxPos + 2 < sizeof(boxes); ++i) {
            const auto& b = people[i];
            const int written = snprintf(boxes + boxPos, sizeof(boxes) - boxPos - 1,
                "%s[%.1f,%.1f,%.1f,%.1f,%.3f]", i ? "," : "", b.x1, b.y1, b.x2, b.y2, b.score);
            if (written < 0 || boxPos + static_cast<size_t>(written) + 2 > sizeof(boxes)) {
                break;
            }
            boxPos += static_cast<size_t>(written);
        }
        boxes[boxPos++] = ']';
        boxes[boxPos] = '\0';
        char metadata[3500], weight[32] = "null", bmi[32] = "null", h[32] = "null", d[32] = "null";
        if (view.hasWeight) snprintf(weight, sizeof(weight), "%.2f", view.weightKg);
        if (view.hasBMI) snprintf(bmi, sizeof(bmi), "%.2f", view.bmi);
        if (view.hasHeight) {
            snprintf(h, sizeof(h), "%.1f", view.heightCm);
            snprintf(d, sizeof(d), "%.3f", view.distanceM);
        }
        snprintf(metadata, sizeof(metadata),
            "{\"kind\":\"frame\",\"width\":%d,\"height\":%d,\"status\":\"%s\",\"boxes\":%s,"
            "\"height_cm\":%s,\"distance_m\":%s,\"weight_kg\":%s,\"weight_stable\":%s,\"bmi\":%s,\"held\":%s,"
            "\"processing_ms\":%lu,\"capture_ms\":%lu,\"free_psram\":%u,"
            "\"free_internal\":%u,\"wifi_rssi\":%d,\"session_id\":%lu}",
            width, height, label, boxes, h, d, weight,
            view.weightStable ? "true" : "false", bmi, view.held ? "true" : "false",
            static_cast<unsigned long>(clockMs() - inferStarted),
            static_cast<unsigned long>(captureMs),
            unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
            unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            wifiRssi(),
            static_cast<unsigned long>(token));
        xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
        publish = s_gate.accepts(token);
        xSemaphoreGive(s_capture_mutex);
        if (publish) {
            usbFrame(jpeg, length, metadata);
        }
    }
}
