#pragma once
// Image settings the YOLO UNO pushes to the OhStem camera (stock firmware, not flashed):
//   GET http://<camera>/control?var=framesize&val=N   and   ...?var=quality&val=Q
// Pure C++ so tests/edge_native_plan2_test.cpp checks URL building and the key ranges.
//
// framesize indices of the LAB camera (esp32-camera enum WITH 240x240), observed:
//   5 = 320x240 (QVGA, verified 18/11)   8 = 640x480 (VGA, the camera default, verified)
//   not yet verified on this camera: 6 = 400x296 (CIF), 7 = 480x320 (HVGA).
// Another camera firmware may use the older enum without 240x240 (4 = QVGA, 6 = VGA),
// which is why the value is configurable and -1 (default) leaves the camera untouched.
// quality: OV2640 JPEG quantizer, LOWER = better image and bigger JPEG (lab used 15).
#include <cstdio>
#include <cstring>

#ifndef EDGE_CAM_FRAMESIZE_MAX
#define EDGE_CAM_FRAMESIZE_MAX 10   // <= 1024x768: decode limit/JPEG_CAPACITY on the UNO
#endif

namespace camera_control {

inline bool framesizeValid(int v) { return v == -1 || (v >= 0 && v <= EDGE_CAM_FRAMESIZE_MAX); }
inline bool qualityValid(int v) { return v == -1 || (v >= 4 && v <= 63); }

// "http://10.127.7.68/capture" -> "http://10.127.7.68/control?var=quality&val=12".
// Keeps host and port, drops the path/query. False on a non-http URL or a short buffer.
inline bool controlUrl(const char* captureUrl, const char* var, int value, char* out, size_t size) {
    if (!captureUrl || !var || !out || size == 0) return false;
    if (std::strncmp(captureUrl, "http://", 7) != 0) return false;
    const char* host = captureUrl + 7;
    size_t hostLen = std::strcspn(host, "/?#");
    if (hostLen == 0) return false;
    const int n = std::snprintf(out, size, "http://%.*s/control?var=%s&val=%d",
                                static_cast<int>(hostLen), host, var, value);
    return n > 0 && static_cast<size_t>(n) < size;
}

// What the capture worker last pushed successfully; re-push only on change.
struct Applied {
    char url[160] = "";
    int framesize = -1, quality = -1;
    bool same(const char* u, int f, int q) const {
        return framesize == f && quality == q && !std::strcmp(url, u);
    }
    void set(const char* u, int f, int q) {
        std::snprintf(url, sizeof(url), "%s", u);
        framesize = f; quality = q;
    }
};

}  // namespace camera_control
