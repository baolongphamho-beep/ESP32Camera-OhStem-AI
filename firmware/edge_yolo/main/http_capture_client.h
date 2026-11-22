#pragma once
// HTTP capture client for the OhStem camera /capture endpoint (ESP-IDF, lwIP sockets).
// Ported from the team GitHub smartscale-edge-ai@78548a6 (firmware/edge_yolo/main/app_main.cpp,
// "stabilize HTTP capture"): non-blocking connect with its own 2.5 s timeout, separate
// header (4.5 s) / body-idle (3 s) / total (6 s) deadlines, Content-Length, chunked and
// EOF bodies (http_capture_parser.h, PC-tested), cached address, TCP_NODELAY and a short
// FIN drain. LAB change: applyCameraFramesize() became applyControl(url, var, value) so
// the cam_framesize / cam_quality keys (camera_control.h) use the same socket path.
// One instance per task (it owns a 4 KiB receive buffer); not thread-safe.
#include "edge_io.h"
#include "http_capture_parser.h"
#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <lwip/inet.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <sys/select.h>
#include <unistd.h>

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

    // GET /control?var=<var>&val=<value> on the camera (framesize, quality, ...).
    bool applyControl(const char* url, const char* var, int value) {
        if (!var || !var[0] || value < 0) return false;
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
            "GET /control?var=%s&val=%d HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Connection: close\r\n"
            "User-Agent: SmartScale/1\r\n\r\n",
            var, value, parsed_.hostHeader);
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
