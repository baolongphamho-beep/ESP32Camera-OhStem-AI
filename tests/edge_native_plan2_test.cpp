#include "../firmware/edge_yolo/main/edge_device_config.h"
#include "../firmware/edge_yolo/main/edge_scale.h"
#include "../firmware/edge_yolo/main/capture_gate.h"
#include "../firmware/edge_yolo/main/camera_test.h"
#include "../firmware/edge_yolo/main/servo_jog.h"
#include "../firmware/edge_yolo/main/edge_geometry.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

int main() {
    EdgeDeviceConfig cfg;
    assert(deviceConfigValid(cfg));
    assert(applyDeviceConfigKey(cfg, "servo_bottom", "65") == ConfigResult::Applied);
    assert(applyDeviceConfigKey(cfg, "servo_top", "115") == ConfigResult::Applied);
    assert(applyDeviceConfigKey(cfg, "servo_dir", "-1") == ConfigResult::Applied);
    // Reserved pins are accepted (team decision 22/11); only 0..48 is checked.
    assert(applyDeviceConfigKey(cfg, "servo_pin", "19") == ConfigResult::Applied && deviceConfigValid(cfg));
    assert(applyDeviceConfigKey(cfg, "servo_pin", "49") == ConfigResult::Invalid);
    assert(applyDeviceConfigKey(cfg, "servo_pin", "10") == ConfigResult::Applied);
    // TV3 keys: camera image age and continuous-sweep speed.
    assert(applyDeviceConfigKey(cfg, "servo_delay", "500") == ConfigResult::Applied && cfg.frameDelayMs == 500);
    assert(applyDeviceConfigKey(cfg, "servo_dps", "4") == ConfigResult::Applied && cfg.speedDps == 4);
    {
        EdgeDeviceConfig bad;
        bad.frameDelayMs = 5000;
        assert(!deviceConfigValid(bad));
        bad = EdgeDeviceConfig(); bad.speedDps = 0;
        assert(!deviceConfigValid(bad));
    }
    assert(applyDeviceConfigKey(cfg, "scaleMac", "70:87:9e:ea:3a:e0") == ConfigResult::Applied);
    assert(!std::strcmp(cfg.scaleMac, "70:87:9E:EA:3A:E0"));
    assert(applyDeviceConfigKey(cfg, "camera", "http://192.168.4.1/capture") == ConfigResult::Applied);
    assert(applyDeviceConfigKey(cfg, "camera", "file:///secrets") == ConfigResult::Invalid);
    assert(applyDeviceConfigKey(cfg, "password", "test\nCFG servo_pin=19") == ConfigResult::Invalid);
    for (const char* bad : {"0", "0.0", "nan", "inf", "-1", "21"})
        assert(applyDeviceConfigKey(cfg, "focal", bad) == ConfigResult::Invalid);
    assert(applyDeviceConfigKey(cfg, "focal", "1.33") == ConfigResult::Applied);
    assert(deviceConfigValid(cfg));
    cfg.topDeg = cfg.bottomDeg;
    assert(!deviceConfigValid(cfg));
    cfg = EdgeDeviceConfig(); cfg.settleMs = 0xffffffff;
    assert(!deviceConfigValid(cfg));
    cfg = EdgeDeviceConfig(); cfg.holdSec = NAN;
    assert(!deviceConfigValid(cfg));

    // Camera image settings pushed over /control (camera_control.h); -1 = untouched.
    cfg = EdgeDeviceConfig();
    assert(cfg.camFramesize == -1 && cfg.camQuality == -1 && deviceConfigValid(cfg));
    assert(applyDeviceConfigKey(cfg, "cam_framesize", "5") == ConfigResult::Applied && cfg.camFramesize == 5);
    assert(applyDeviceConfigKey(cfg, "cam_quality", "12") == ConfigResult::Applied && cfg.camQuality == 12);
    assert(applyDeviceConfigKey(cfg, "cam_quality", "-1") == ConfigResult::Applied && cfg.camQuality == -1);
    for (const char* bad : {"3", "64", "12.5", "", "abc", "nan"})
        assert(applyDeviceConfigKey(cfg, "cam_quality", bad) == ConfigResult::Invalid);
    for (const char* bad : {"-2", "11", "13", "5.5"})
        assert(applyDeviceConfigKey(cfg, "cam_framesize", bad) == ConfigResult::Invalid);
    assert(cfg.camFramesize == 5 && deviceConfigValid(cfg));
    cfg.camQuality = 2;
    assert(!deviceConfigValid(cfg));  // Corrupt NVS value -> defaults restored on boot.
    // The Arduino firmware ignores these keys, so one viewer serves both.
    EdgeConfig arduino;
    assert(applyConfigKey(arduino, "cam_quality", "12") == ConfigResult::Ignored);
    assert(applyConfigKey(arduino, "cam_framesize", "5") == ConfigResult::Ignored);

    char url[96];
    assert(camera_control::controlUrl("http://10.127.7.68/capture", "quality", 12, url, sizeof(url)));
    assert(!std::strcmp(url, "http://10.127.7.68/control?var=quality&val=12"));
    assert(camera_control::controlUrl("http://192.168.4.1:8080/capture?x=1", "framesize", 5, url, sizeof(url)));
    assert(!std::strcmp(url, "http://192.168.4.1:8080/control?var=framesize&val=5"));
    assert(camera_control::controlUrl("http://cam", "quality", 10, url, sizeof(url)));
    assert(!std::strcmp(url, "http://cam/control?var=quality&val=10"));
    assert(!camera_control::controlUrl("https://cam/capture", "quality", 10, url, sizeof(url)));
    assert(!camera_control::controlUrl("http:///capture", "quality", 10, url, sizeof(url)));
    char tiny[16];
    assert(!camera_control::controlUrl("http://10.127.7.68/capture", "quality", 10, tiny, sizeof(tiny)));
    camera_control::Applied applied;
    assert(!applied.same("http://cam/capture", 5, 12));
    applied.set("http://cam/capture", 5, 12);
    assert(applied.same("http://cam/capture", 5, 12) && !applied.same("http://cam/capture", 5, 10));
    assert(!applied.same("http://cam2/capture", 5, 12));

    const uint8_t address[] = {0xe0, 0x3a, 0xea, 0x9e, 0x87, 0x70};
    assert(scaleAddressMatches(address, "70:87:9e:ea:3a:e0"));
    assert(!scaleAddressMatches(address, "70:87:9e:ea:3a:e1"));
    assert(!scaleAddressMatches(address, "invalid"));
    // Flags AD before service data; weight=65 kg, stabilized.
    uint8_t ad[] = {2, 1, 6, 6, 0x16, 0x1d, 0x18, 0x20, 0xc8, 0x32};
    ScaleReading reading;
    assert(decodeScaleAdvertisement(ad, sizeof(ad), reading));
    assert(reading.present && reading.stable && std::fabs(reading.kg - 65) < .001f);
    assert(!decodeScaleAdvertisement(ad, sizeof(ad) - 1, reading));
    ad[7] = 0xa0;
    assert(decodeScaleAdvertisement(ad, sizeof(ad), reading));
    assert(!reading.present && !reading.stable && reading.kg == 0);
    ad[7] = 0x21; ad[8] = 0x10; ad[9] = 0x27; // 100 lb.
    assert(decodeScaleAdvertisement(ad, sizeof(ad), reading));
    assert(std::fabs(reading.kg - 45.359237f) < .001f);
    for (size_t n = 0; n < sizeof(ad); ++n) assert(!decodeScaleAdvertisement(ad, n, reading));

    CaptureGate gate;
    assert(!gate.canRequest(SystemPhase::Standby, true));
    assert(!gate.canRequest(SystemPhase::WakingHoming, true));
    assert(!gate.canRequest(SystemPhase::ServoSweep, false));
    assert(gate.canRequest(SystemPhase::ServoSweep, true));
    auto token = gate.begin();
    assert(!gate.canRequest(SystemPhase::ServoSweep, true));
    gate.invalidate(); // Servo moved, person left, or USB configuration changed.
    assert(!gate.current(token));
    assert(!gate.canRequest(SystemPhase::ServoSweep, true)); // Worker still owns buffer.
    assert(!gate.finish(token));
    assert(gate.canRequest(SystemPhase::LockedMeasure, true));
    token = gate.begin(); assert(gate.finish(token));

    // Camera-only test captures from STANDBY without a settled/attached servo.
    assert(gate.canRequest(SystemPhase::Standby, false, true));
    token = gate.begin();
    assert(!gate.canRequest(SystemPhase::Standby, false, true));
    gate.invalidate(); // Leaving test mode invalidates its queued image.
    assert(!gate.finish(token));
    assert(!gate.canRequest(SystemPhase::Standby, false));
    // USB "CONFIG?" read-back (deviceConfigJson): wire key names, never the password.
    {
        EdgeDeviceConfig c;
        std::strcpy(c.ssid, "Lab \"5G\"\\x");
        std::strcpy(c.password, "SuperSecret123");
        c.levelDeg = 92.5f; c.dir = 1; c.bottomDeg = 130; c.topDeg = 60; c.camQuality = 12;
        char json[768];
        assert(deviceConfigJson(c, json, sizeof(json)));
        assert(!std::strstr(json, "SuperSecret") && std::strstr(json, "\"password_set\":true"));
        for (const char* part : {"\"kind\":\"config_values\"", "\"servo_level\":92.50", "\"servo_dir\":1,",
                                 "\"servo_bottom\":130.00", "\"servo_top\":60.00", "\"servo_delay\":500",
                                 "\"cam_quality\":12", "\"mac\":\"70:87:9E:EA:3A:E0\"",
                                 "\"ssid\":\"Lab ?5G??x\"", "\"servo_limits\":[45.0,135.0]"})
            assert(std::strstr(json, part));
        // Balanced braces/quotes: a quote or backslash in the SSID cannot break the JSON.
        int quotes = 0;
        for (const char* q = json; *q; ++q) quotes += *q == '"';
        assert(quotes % 2 == 0 && json[std::strlen(json) - 1] == '}');
        c.password[0] = '\0';
        assert(deviceConfigJson(c, json, sizeof(json)) && std::strstr(json, "\"password_set\":false"));
        char tiny[32];
        assert(!deviceConfigJson(c, tiny, sizeof(tiny)));
    }
    // USB servo jog for calibration (servo_jog.h).
    {
        float d = -1;
        assert(parseServoJog("SERVO 92.5", d) == ServoJogCommand::Move && d == 92.5f);
        assert(parseServoJog("SERVO  45", d) == ServoJogCommand::Move && d == 45);
        assert(parseServoJog("SERVO 135", d) == ServoJogCommand::Move && d == 135);
        for (const char* outside : {"SERVO 0", "SERVO 44.9", "SERVO 135.1", "SERVO 180"})
            assert(parseServoJog(outside, d) == ServoJogCommand::Invalid);  // hard 45..135
        assert(parseServoJog("SERVO OFF", d) == ServoJogCommand::Off);
        for (const char* bad : {"SERVO", "SERVO ", "SERVO 181", "SERVO -1", "SERVO nan", "SERVO 9x", "SERVO inf"})
            assert(parseServoJog(bad, d) == ServoJogCommand::Invalid);
        for (const char* other : {"PREVIEW ON", "BEGIN", "SERVOX 1", "servo 90", ""})
            assert(parseServoJog(other, d) == ServoJogCommand::None);
        ServoJogLease jog;
        assert(!jog.active(0));
        jog.renew(1000, 92.54f);
        assert(jog.active(1000) && jog.active(10999) && !jog.active(11000));
        assert(std::fabs(jog.deg() - 92.5f) < 1e-4f);
        jog.renew(0xfffffff0u, 45); assert(jog.active(100) && jog.deg() == 45);
        EdgeDeviceConfig limits;
        for (const char* k : {"servo_bottom", "servo_top", "servo_level"})
            assert(applyDeviceConfigKey(limits, k, "30") == ConfigResult::Invalid);
        jog.stop(); assert(!jog.active(101));
    }
    CameraTestLease lease;
    assert(!lease.active(0));
    lease.renew(100);
    assert(lease.active(100) && lease.active(10099));
    assert(!lease.active(10100));
    lease.renew(10100); assert(lease.active(10100));
    lease.stop(); assert(!lease.active(10101));
    lease.renew(0xfffffff0u);
    assert(lease.active(20)); // millis wrap does not end a fresh lease.

    // BLE-only wake works before there is any camera/MeasurementState presence.
    EdgeFsm fsm;
    fsm.begin(0); fsm.setScale(10, true, 65, false); fsm.update(10);
    assert(fsm.phase() == SystemPhase::WakingHoming);
    fsm.setScale(1200, true, 65, false); fsm.update(1210);
    assert(fsm.phase() == SystemPhase::ServoSweep);
    fsm.setScale(1220, false, 0, false); fsm.update(4000);
    assert(fsm.phase() == SystemPhase::Standby);

    // Shared framing on native letterboxed coordinates (safe margin ~7 px).
    auto box = originalBox({80, 40, 140, 180, .9f}, 320, 240);
    assert(analyzeFraming(box, 320, 240, 7.2f) == FramingStatus::FullBody);
    assert(analyzeFraming({100, 0, 200, 240, .9f}, 320, 240, 7.2f) == FramingStatus::TooClose);
    // Different model inputs must map the same physical person back to VGA.
    for (const int input : {160, 192, 224}) {
        const float unit = input / 160.f;
        auto mapped = originalBox({40 * unit, 30 * unit, 120 * unit, 130 * unit, .9f},
                                  640, 480, input);
        assert(std::fabs(mapped.x1 - 160) < .01f && std::fabs(mapped.y1 - 40) < .01f);
        assert(std::fabs(mapped.x2 - 480) < .01f && std::fabs(mapped.y2 - 440) < .01f);
    }
    std::cout << "Native Plan 2 config/BLE/capture freshness/geometry PASS\n";
}
