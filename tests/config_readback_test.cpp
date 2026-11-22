// g++ -std=c++17 tests/config_readback_test.cpp -o config_readback_test && ./config_readback_test
// TV5: USB "CONFIG?" reply (firmware/edge_yolo/main/config_readback.h).
#undef NDEBUG
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "../firmware/edge_yolo/main/config_readback.h"

// Same field names/types as EdgeConfig in firmware/edge_yolo/main/edge_io.h.
struct FakeConfig {
    char ssid[33] = {};
    char password[65] = {};
    char cameraUrl[160] = "http://10.127.7.68/capture";
    char scaleMac[18] = "70:87:9e:ea:3a:e0";
    float distanceM = 3;
    float referenceCm = 183;
    float focalPx = 0;
    float cameraHeightCm = 0;
    float tiltDeg = 0;
    bool groundMode = false;
    uint16_t holdSec = 10;
    int8_t servoPin = 10;
    float servoMinDeg = 45, servoMaxDeg = 135, servoBottomDeg = 112, servoTopDeg = 85;
    float servoStepDeg = 4.5f, servoLevelDeg = 90;
    int8_t servoDir = 1;
    uint16_t servoSettleMs = 200;
    uint32_t revision = 7;
};

int main() {
    FakeConfig c;
    std::strcpy(c.ssid, "Lab \"5G\"\\x");
    std::strcpy(c.password, "SuperSecret123");
    c.groundMode = true; c.cameraHeightCm = 110; c.focalPx = 1.33f; c.servoLevelDeg = 92.5f;
    char json[1024];
    assert(config_readback::toJson(c, json, sizeof(json)));
    assert(!std::strstr(json, "SuperSecret"));
    for (const char* part : {"\"kind\":\"config_values\"", "\"password_set\":true", "\"ssid\":\"Lab ?5G??x\"",
                             "\"mode\":\"ground\"", "\"height\":110.0", "\"focal\":1.3300",
                             "\"hold_sec\":10", "\"servo_pin\":10", "\"servo_min\":45.0", "\"servo_max\":135.0",
                             "\"servo_bottom\":112.0", "\"servo_top\":85.0", "\"servo_step\":4.50",
                             "\"servo_level\":92.5", "\"servo_dir\":1,", "\"servo_settle\":200", "\"revision\":7}",
                             "\"mac\":\"70:87:9e:ea:3a:e0\"", "\"camera\":\"http://10.127.7.68/capture\""})
        assert(std::strstr(json, part));
    int quotes = 0;
    for (const char* q = json; *q; ++q) quotes += *q == '"';
    assert(quotes % 2 == 0 && json[std::strlen(json) - 1] == '}');
    c.password[0] = '\0';
    assert(config_readback::toJson(c, json, sizeof(json)) && std::strstr(json, "\"password_set\":false"));
    char tiny[40];
    assert(!config_readback::toJson(c, tiny, sizeof(tiny)));
    std::puts("config_readback_test: OK");
    return 0;
}
