#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

struct ParsedHttpUrl {
    char host[96] = {};
    char hostHeader[108] = {};
    char path[160] = {};
    uint16_t port = 80;
};

struct HttpResponseHead {
    int statusCode = 0;
    int httpMinor = 1;
    size_t headerBytes = 0;
    size_t contentLength = 0;
    bool hasContentLength = false;
    bool isChunked = false;
    bool keepAlive = true;
};

enum class HeaderParseStatus {
    Incomplete,
    Complete,
    Invalid,
};

namespace http_detail {

inline bool asciiEqualsIgnoreCase(const char* a, size_t aLen, const char* b) {
    const size_t bLen = strlen(b);
    if (aLen != bLen) return false;
    for (size_t i = 0; i < aLen; ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

inline void trimOws(const char* data, size_t& start, size_t& end) {
    while (start < end && (data[start] == ' ' || data[start] == '\t')) ++start;
    while (end > start && (data[end - 1] == ' ' || data[end - 1] == '\t')) --end;
}

inline bool headerListHasToken(const char* val, size_t len, const char* token) {
    size_t i = 0;
    while (i < len) {
        size_t comma = i;
        while (comma < len && val[comma] != ',') ++comma;
        size_t tStart = i;
        size_t tEnd = comma;
        trimOws(val, tStart, tEnd);
        if (asciiEqualsIgnoreCase(val + tStart, tEnd - tStart, token)) {
            return true;
        }
        i = comma + 1;
    }
    return false;
}

inline int hexValue(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

} // namespace http_detail

inline bool parseHttpUrl(const char* url, ParsedHttpUrl& out) {
    out = ParsedHttpUrl{};
    if (!url || !url[0]) return false;

    const char* prefix = "http://";
    for (size_t i = 0; i < 7; ++i) {
        if (std::tolower(static_cast<unsigned char>(url[i])) != prefix[i]) {
            return false;
        }
    }

    const char* rest = url + 7;
    const size_t restLen = strlen(rest);
    if (restLen == 0) return false;

    for (size_t i = 0; i < restLen; ++i) {
        const unsigned char c = static_cast<unsigned char>(rest[i]);
        if (c < 0x21 || c >= 0x7f) {
            return false;
        }
    }

    size_t authEnd = 0;
    while (authEnd < restLen && rest[authEnd] != '/' && rest[authEnd] != '?') {
        if (rest[authEnd] == '@' || rest[authEnd] == '#') {
            return false;
        }
        ++authEnd;
    }
    if (authEnd == 0) return false;

    size_t colonPos = authEnd;
    for (size_t i = 0; i < authEnd; ++i) {
        if (rest[i] == ':') {
            if (colonPos != authEnd) return false; // Multiple colons (unbracketed IPv6 not supported)
            colonPos = i;
        }
    }

    const size_t hostLen = colonPos;
    if (hostLen == 0 || hostLen >= sizeof(out.host)) return false;
    memcpy(out.host, rest, hostLen);
    out.host[hostLen] = '\0';

    if (colonPos < authEnd) {
        const size_t portStart = colonPos + 1;
        if (portStart >= authEnd) return false;
        uint32_t portVal = 0;
        for (size_t i = portStart; i < authEnd; ++i) {
            const unsigned char c = static_cast<unsigned char>(rest[i]);
            if (c < '0' || c > '9') return false;
            portVal = portVal * 10U + static_cast<uint32_t>(c - '0');
            if (portVal > 65535U) return false;
        }
        if (portVal == 0U) return false;
        out.port = static_cast<uint16_t>(portVal);
    } else {
        out.port = 80;
    }

    if (out.port == 80) {
        if (hostLen >= sizeof(out.hostHeader)) return false;
        memcpy(out.hostHeader, out.host, hostLen + 1);
    } else {
        const int n = snprintf(out.hostHeader, sizeof(out.hostHeader), "%s:%u", out.host, unsigned(out.port));
        if (n <= 0 || static_cast<size_t>(n) >= sizeof(out.hostHeader)) return false;
    }

    if (authEnd == restLen) {
        strcpy(out.path, "/");
    } else if (rest[authEnd] == '/') {
        const size_t pathLen = restLen - authEnd;
        if (pathLen >= sizeof(out.path)) return false;
        memcpy(out.path, rest + authEnd, pathLen + 1);
    } else {
        const size_t pathLen = restLen - authEnd;
        if (pathLen + 1 >= sizeof(out.path)) return false;
        out.path[0] = '/';
        memcpy(out.path + 1, rest + authEnd, pathLen + 1);
    }

    return true;
}

inline HeaderParseStatus parseHttpResponseHead(const uint8_t* data, size_t len, HttpResponseHead& out) {
    out = HttpResponseHead{};
    if (!data || len < 4) return HeaderParseStatus::Incomplete;

    size_t eoh = SIZE_MAX;
    for (size_t i = 0; i + 3 < len; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n') {
            eoh = i;
            break;
        }
    }
    if (eoh == SIZE_MAX) {
        return HeaderParseStatus::Incomplete;
    }

    out.headerBytes = eoh + 4;
    const char* text = reinterpret_cast<const char*>(data);

    size_t lineEnd = 0;
    while (lineEnd < eoh && !(text[lineEnd] == '\r' && text[lineEnd + 1] == '\n')) {
        ++lineEnd;
    }

    // Status line minimum: "HTTP/1.x NNN" (12 chars)
    if (lineEnd < 12 || memcmp(text, "HTTP/1.", 7) != 0) {
        return HeaderParseStatus::Invalid;
    }
    if (text[7] != '0' && text[7] != '1') {
        return HeaderParseStatus::Invalid;
    }
    out.httpMinor = text[7] - '0';
    if (text[8] != ' ') {
        return HeaderParseStatus::Invalid;
    }

    int statusCode = 0;
    for (size_t i = 9; i < 12; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < '0' || c > '9') return HeaderParseStatus::Invalid;
        statusCode = statusCode * 10 + (c - '0');
    }
    if (statusCode < 100 || statusCode > 599) {
        return HeaderParseStatus::Invalid;
    }
    if (lineEnd > 12 && text[12] != ' ') {
        return HeaderParseStatus::Invalid;
    }
    out.statusCode = statusCode;
    out.keepAlive = (out.httpMinor >= 1);

    bool sawConnClose = false;
    bool sawConnKeepAlive = false;

    size_t pos = lineEnd + 2;
    while (pos < eoh) {
        size_t nextEnd = pos;
        while (nextEnd < eoh && !(text[nextEnd] == '\r' && text[nextEnd + 1] == '\n')) {
            ++nextEnd;
        }

        size_t colon = pos;
        while (colon < nextEnd && text[colon] != ':') {
            if (text[colon] == ' ' || text[colon] == '\t' ||
                static_cast<unsigned char>(text[colon]) < 0x20) {
                return HeaderParseStatus::Invalid;
            }
            ++colon;
        }
        if (colon == pos || colon >= nextEnd) {
            return HeaderParseStatus::Invalid;
        }

        const char* name = text + pos;
        const size_t nameLen = colon - pos;
        size_t valStart = colon + 1;
        size_t valEnd = nextEnd;
        http_detail::trimOws(text, valStart, valEnd);
        const char* val = text + valStart;
        const size_t valLen = valEnd - valStart;

        if (http_detail::asciiEqualsIgnoreCase(name, nameLen, "Content-Length")) {
            if (valLen == 0) return HeaderParseStatus::Invalid;
            static constexpr uint64_t kMaxContentLength = 100ULL * 1024ULL * 1024ULL;
            uint64_t parsedLen = 0;
            for (size_t i = 0; i < valLen; ++i) {
                const unsigned char c = static_cast<unsigned char>(val[i]);
                if (c < '0' || c > '9') return HeaderParseStatus::Invalid;
                parsedLen = parsedLen * 10ULL + static_cast<uint64_t>(c - '0');
                if (parsedLen > kMaxContentLength) return HeaderParseStatus::Invalid;
            }
            if (out.hasContentLength && out.contentLength != static_cast<size_t>(parsedLen)) {
                return HeaderParseStatus::Invalid;
            }
            out.hasContentLength = true;
            out.contentLength = static_cast<size_t>(parsedLen);
        } else if (http_detail::asciiEqualsIgnoreCase(name, nameLen, "Transfer-Encoding")) {
            if (http_detail::headerListHasToken(val, valLen, "chunked")) {
                out.isChunked = true;
            }
        } else if (http_detail::asciiEqualsIgnoreCase(name, nameLen, "Connection")) {
            if (http_detail::headerListHasToken(val, valLen, "close")) {
                sawConnClose = true;
            }
            if (http_detail::headerListHasToken(val, valLen, "keep-alive")) {
                sawConnKeepAlive = true;
            }
        }

        pos = nextEnd + 2;
    }

    if (sawConnClose) {
        out.keepAlive = false;
    } else if (sawConnKeepAlive) {
        out.keepAlive = true;
    }

    // Per RFC 9112 section 6.3, Transfer-Encoding overrides Content-Length.
    if (out.isChunked) {
        out.hasContentLength = false;
        out.contentLength = 0;
    }

    return HeaderParseStatus::Complete;
}

class ChunkedBodyDecoder {
public:
    ChunkedBodyDecoder() { reset(); }

    void reset() {
        state_ = State::SizeLine;
        chunkRemaining_ = 0;
        hexDigits_ = 0;
        inExtension_ = false;
        sawBws_ = false;
        overflow_ = false;
        extraBytes_ = 0;
    }

    bool feed(const uint8_t* in, size_t inLen, uint8_t* out, size_t outCap, size_t& outUsed) {
        if (state_ == State::Error) return false;
        if (outUsed > outCap) {
            overflow_ = true;
            state_ = State::Error;
            return false;
        }
        if (!in || inLen == 0) return true;

        size_t i = 0;
        while (i < inLen) {
            if (state_ == State::Done) {
                extraBytes_ += (inLen - i);
                break;
            }
            if (state_ == State::Error) {
                return false;
            }

            if (state_ == State::ChunkData) {
                const size_t take = std::min(chunkRemaining_, inLen - i);
                if (take > outCap - outUsed) {
                    overflow_ = true;
                    state_ = State::Error;
                    return false;
                }
                if (out && take > 0) {
                    memcpy(out + outUsed, in + i, take);
                }
                outUsed += take;
                chunkRemaining_ -= take;
                i += take;
                if (chunkRemaining_ == 0) {
                    state_ = State::ChunkDataCr;
                }
                continue;
            }

            const uint8_t b = in[i++];
            switch (state_) {
                case State::SizeLine:
                    if (b == '\r') {
                        if (hexDigits_ == 0) {
                            state_ = State::Error;
                            return false;
                        }
                        state_ = State::SizeLf;
                    } else if (b == '\n') {
                        state_ = State::Error;
                        return false;
                    } else if (inExtension_) {
                        // Chunk extension runs until CRLF; ignore cleanly.
                    } else if (b == ';') {
                        if (hexDigits_ == 0) {
                            state_ = State::Error;
                            return false;
                        }
                        inExtension_ = true;
                    } else if (b == ' ' || b == '\t') {
                        if (hexDigits_ == 0) {
                            state_ = State::Error;
                            return false;
                        }
                        sawBws_ = true;
                    } else {
                        if (sawBws_) {
                            state_ = State::Error;
                            return false;
                        }
                        const int hv = http_detail::hexValue(b);
                        if (hv < 0) {
                            state_ = State::Error;
                            return false;
                        }
                        static constexpr size_t kMaxChunkBytes = 100U * 1024U * 1024U;
                        if (chunkRemaining_ > (kMaxChunkBytes - static_cast<size_t>(hv)) / 16U) {
                            state_ = State::Error;
                            return false;
                        }
                        chunkRemaining_ = chunkRemaining_ * 16U + static_cast<size_t>(hv);
                        ++hexDigits_;
                    }
                    break;

                case State::SizeLf:
                    if (b != '\n') {
                        state_ = State::Error;
                        return false;
                    }
                    if (chunkRemaining_ == 0) {
                        state_ = State::TrailerLineStart;
                    } else {
                        if (chunkRemaining_ > outCap - outUsed) {
                            overflow_ = true;
                            state_ = State::Error;
                            return false;
                        }
                        state_ = State::ChunkData;
                    }
                    break;

                case State::ChunkDataCr:
                    if (b != '\r') {
                        state_ = State::Error;
                        return false;
                    }
                    state_ = State::ChunkDataLf;
                    break;

                case State::ChunkDataLf:
                    if (b != '\n') {
                        state_ = State::Error;
                        return false;
                    }
                    chunkRemaining_ = 0;
                    hexDigits_ = 0;
                    inExtension_ = false;
                    sawBws_ = false;
                    state_ = State::SizeLine;
                    break;

                case State::TrailerLineStart:
                    if (b == '\r') {
                        state_ = State::TrailerEndLf;
                    } else if (b == '\n') {
                        state_ = State::Error;
                        return false;
                    } else {
                        state_ = State::TrailerLineBody;
                    }
                    break;

                case State::TrailerLineBody:
                    if (b == '\r') {
                        state_ = State::TrailerLineLf;
                    } else if (b == '\n') {
                        state_ = State::Error;
                        return false;
                    }
                    break;

                case State::TrailerLineLf:
                    if (b != '\n') {
                        state_ = State::Error;
                        return false;
                    }
                    state_ = State::TrailerLineStart;
                    break;

                case State::TrailerEndLf:
                    if (b != '\n') {
                        state_ = State::Error;
                        return false;
                    }
                    state_ = State::Done;
                    break;

                case State::ChunkData:
                case State::Done:
                case State::Error:
                    break;
            }
        }
        return state_ != State::Error;
    }

    bool done() const { return state_ == State::Done; }
    bool error() const { return state_ == State::Error; }
    bool overflow() const { return overflow_; }
    size_t extraBytes() const { return extraBytes_; }

private:
    enum class State {
        SizeLine,
        SizeLf,
        ChunkData,
        ChunkDataCr,
        ChunkDataLf,
        TrailerLineStart,
        TrailerLineBody,
        TrailerLineLf,
        TrailerEndLf,
        Done,
        Error,
    };

    State state_ = State::SizeLine;
    size_t chunkRemaining_ = 0;
    size_t hexDigits_ = 0;
    bool inExtension_ = false;
    bool sawBws_ = false;
    bool overflow_ = false;
    size_t extraBytes_ = 0;
};

inline size_t trimAndValidateJpeg(const uint8_t* buf, size_t len) {
    if (!buf || len < 4 || buf[0] != 0xff || buf[1] != 0xd8) {
        return 0;
    }
    size_t end = len;
    while (end >= 4 && len - end < 1024 && !(buf[end - 2] == 0xff && buf[end - 1] == 0xd9)) {
        --end;
    }
    if (end < 4 || !(buf[end - 2] == 0xff && buf[end - 1] == 0xd9)) {
        return 0;
    }
    return end;
}
