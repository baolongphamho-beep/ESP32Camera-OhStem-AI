#include "../firmware/edge_yolo/main/http_capture_parser.h"
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

static void testParseHttpUrl() {
    ParsedHttpUrl u;

    assert(parseHttpUrl("http://10.127.7.68/capture", u));
    assert(strcmp(u.host, "10.127.7.68") == 0);
    assert(strcmp(u.hostHeader, "10.127.7.68") == 0);
    assert(strcmp(u.path, "/capture") == 0);
    assert(u.port == 80);

    assert(parseHttpUrl("HTTP://cam.local:8080/capture?_cb=42", u));
    assert(strcmp(u.host, "cam.local") == 0);
    assert(strcmp(u.hostHeader, "cam.local:8080") == 0);
    assert(strcmp(u.path, "/capture?_cb=42") == 0);
    assert(u.port == 8080);

    assert(parseHttpUrl("http://10.127.7.68", u));
    assert(strcmp(u.host, "10.127.7.68") == 0);
    assert(strcmp(u.hostHeader, "10.127.7.68") == 0);
    assert(strcmp(u.path, "/") == 0);
    assert(u.port == 80);

    assert(parseHttpUrl("http://10.127.7.68?t=1", u));
    assert(strcmp(u.path, "/?t=1") == 0);

    assert(!parseHttpUrl(nullptr, u));
    assert(!parseHttpUrl("", u));
    assert(!parseHttpUrl("https://10.127.7.68/capture", u));
    assert(!parseHttpUrl("10.127.7.68/capture", u));
    assert(!parseHttpUrl("http:///capture", u));
    assert(!parseHttpUrl("http://10.127.7.68:/capture", u));
    assert(!parseHttpUrl("http://10.127.7.68:0/capture", u));
    assert(!parseHttpUrl("http://10.127.7.68:65536/capture", u));
    assert(!parseHttpUrl("http://10.127.7.68:abc/capture", u));
    assert(!parseHttpUrl("http://user@10.127.7.68/capture", u));
    assert(!parseHttpUrl("http://10.127.7.68/capture\r\nX-Inject: 1", u));
    assert(!parseHttpUrl("http://10.127.7.68/cap ture", u));

    std::string longPath(170, 'a');
    std::string longUrl = "http://10.0.0.1/" + longPath;
    assert(!parseHttpUrl(longUrl.c_str(), u));
}

static void testParseHttpResponseHead() {
    HttpResponseHead head;

    const char* partial = "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n";
    assert(parseHttpResponseHead(reinterpret_cast<const uint8_t*>(partial), strlen(partial), head) ==
           HeaderParseStatus::Incomplete);

    const char* okLen = "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\ncontent-length:  12345 \r\n\r\nBODY";
    assert(parseHttpResponseHead(reinterpret_cast<const uint8_t*>(okLen), strlen(okLen), head) ==
           HeaderParseStatus::Complete);
    assert(head.statusCode == 200);
    assert(head.httpMinor == 1);
    assert(head.hasContentLength && head.contentLength == 12345);
    assert(!head.isChunked);
    assert(head.keepAlive);
    assert(memcmp(okLen + head.headerBytes, "BODY", 4) == 0);

    const char* closeBoth = "HTTP/1.1 200\r\nConnection: keep-alive, CLOSE\r\nContent-Length: 4\r\n\r\n";
    assert(parseHttpResponseHead(reinterpret_cast<const uint8_t*>(closeBoth), strlen(closeBoth), head) ==
           HeaderParseStatus::Complete);
    assert(!head.keepAlive);

    const char* http10 = "HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\n";
    assert(parseHttpResponseHead(reinterpret_cast<const uint8_t*>(http10), strlen(http10), head) ==
           HeaderParseStatus::Complete);
    assert(!head.keepAlive);

    const char* http10Ka = "HTTP/1.0 200 OK\r\nConnection: Keep-Alive\r\n\r\n";
    assert(parseHttpResponseHead(reinterpret_cast<const uint8_t*>(http10Ka), strlen(http10Ka), head) ==
           HeaderParseStatus::Complete);
    assert(head.keepAlive);

    // Transfer-Encoding chunked must override Content-Length.
    const char* chunkedOverride =
        "HTTP/1.1 200 OK\r\nContent-Length: 999\r\nTransfer-Encoding: gzip, ChUnKeD\r\n\r\n";
    assert(parseHttpResponseHead(reinterpret_cast<const uint8_t*>(chunkedOverride), strlen(chunkedOverride), head) ==
           HeaderParseStatus::Complete);
    assert(head.isChunked);
    assert(!head.hasContentLength);
    assert(head.contentLength == 0);

    // Conflicting Content-Length must fail.
    const char* conflictLen = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\n";
    assert(parseHttpResponseHead(reinterpret_cast<const uint8_t*>(conflictLen), strlen(conflictLen), head) ==
           HeaderParseStatus::Invalid);

    // Invalid status / malformed header line.
    const char* badVer = "HTTP/1.2 200 OK\r\n\r\n";
    assert(parseHttpResponseHead(reinterpret_cast<const uint8_t*>(badVer), strlen(badVer), head) ==
           HeaderParseStatus::Invalid);

    const char* badHdr = "HTTP/1.1 200 OK\r\nBad Header : 1\r\n\r\n";
    assert(parseHttpResponseHead(reinterpret_cast<const uint8_t*>(badHdr), strlen(badHdr), head) ==
           HeaderParseStatus::Invalid);
}

static void testChunkedBodyDecoder() {
    ChunkedBodyDecoder dec;
    uint8_t out[64];
    size_t used = 0;

    // Chunked payload containing embedded JPEG EOI 0xFF 0xD9 inside the first chunk,
    // chunk extension, second chunk, and trailers.
    const uint8_t payload[] = {
        '6', ';', 'f', 'o', 'o', '=', '1', '\r', '\n',
        0xff, 0xd8, 0x11, 0xff, 0xd9, 0x22, '\r', '\n',
        '3', '\r', '\n',
        0x33, 0xff, 0xd9, '\r', '\n',
        '0', '\r', '\n',
        'X', '-', 'T', ':', ' ', 'v', '\r', '\n',
        '\r', '\n'
    };

    // Byte-by-byte feeding ensures state machine handles arbitrary packet splits.
    for (size_t i = 0; i < sizeof(payload); ++i) {
        assert(dec.feed(&payload[i], 1, out, sizeof(out), used));
        if (i + 1 < sizeof(payload)) {
            assert(!dec.done());
        }
    }
    assert(dec.done());
    assert(!dec.error());
    assert(!dec.overflow());
    assert(dec.extraBytes() == 0);
    assert(used == 9);
    const uint8_t expected[9] = {0xff, 0xd8, 0x11, 0xff, 0xd9, 0x22, 0x33, 0xff, 0xd9};
    assert(memcmp(out, expected, sizeof(expected)) == 0);

    // Overflow protection.
    dec.reset();
    used = 0;
    const char* over = "a\r\n0123456789\r\n0\r\n\r\n";
    assert(!dec.feed(reinterpret_cast<const uint8_t*>(over), strlen(over), out, 8, used));
    assert(dec.error() && dec.overflow() && !dec.done());

    // Trailing pipelined bytes counted via extraBytes().
    dec.reset();
    used = 0;
    const char* withExtra = "2\r\nhi\r\n0\r\n\r\nEXTRA";
    assert(dec.feed(reinterpret_cast<const uint8_t*>(withExtra), strlen(withExtra), out, sizeof(out), used));
    assert(dec.done() && !dec.error() && used == 2 && dec.extraBytes() == 5);
    assert(memcmp(out, "hi", 2) == 0);

    // Invalid hex chunk size.
    dec.reset();
    used = 0;
    const char* badChunk = "Z\r\n";
    assert(!dec.feed(reinterpret_cast<const uint8_t*>(badChunk), strlen(badChunk), out, sizeof(out), used));
    assert(dec.error() && !dec.overflow());
}

static void testTrimAndValidateJpeg() {
    std::vector<uint8_t> jpg = {0xff, 0xd8, 0xaa, 0xbb, 0xff, 0xd9, 0x00, 0x00, 0x00};
    assert(trimAndValidateJpeg(jpg.data(), jpg.size()) == 6);

    const uint8_t tiny[4] = {0xff, 0xd8, 0xff, 0xd9};
    assert(trimAndValidateJpeg(tiny, sizeof(tiny)) == 4);

    const uint8_t tooShort[3] = {0xff, 0xd8, 0xff};
    assert(trimAndValidateJpeg(tooShort, sizeof(tooShort)) == 0);

    const char* busy = "{\"busy\":1}";
    assert(trimAndValidateJpeg(reinterpret_cast<const uint8_t*>(busy), strlen(busy)) == 0);

    // Excessive padding (>= 1024 B after EOI) must not match.
    std::vector<uint8_t> excess(1030, 0x00);
    excess[0] = 0xff;
    excess[1] = 0xd8;
    excess[2] = 0xff;
    excess[3] = 0xd9;
    assert(trimAndValidateJpeg(excess.data(), excess.size()) == 0);
}

int main() {
    testParseHttpUrl();
    testParseHttpResponseHead();
    testChunkedBodyDecoder();
    testTrimAndValidateJpeg();
    return 0;
}
