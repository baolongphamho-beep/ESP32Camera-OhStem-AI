// g++ -std=c++17 -I lib/EdgeSweep tests/edge_sweep_logic_test.cpp -o .pio/slt.exe; .pio/slt.exe
// Config keys, geometry, framing, UART messages, LCD text (TV5-2) and SSV1 packets.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <type_traits>

#include "edge_geometry.h"
#include "edge_message.h"
#include "edge_io.h"
#include "edge_config.h"
#include "usb_packet.h"
#include "servo_motion.h"

using namespace edge_geometry;

static bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

// Image row of a ray `angle` deg below horizontal, camera tilted `tilt` deg down.
static float rowFor(float angle, float tilt, float focal) {
    return 0.5f + focal * std::tan(rad(angle - tilt));
}

static void testConfig() {
    EdgeConfig c;
    // TV3 defaults (servo_ctrl.zip 22/11): 45..135 deg, step 5, settle 200, camera delay 500, dir -1.
    assert(c.servoPin == 10 && c.bottomDeg == 45 && c.topDeg == 135 && c.holdMs() == 12000);
    assert(c.stepDeg == 5 && c.settleMs == 200 && c.frameDelayMs == 500 && c.dir == -1 && c.speedDps == 4);
    assert(applyConfigKey(c, "servo_delay", "650") == ConfigResult::Applied && c.frameDelayMs == 650);
    assert(applyConfigKey(c, "servo_dps", "12.5") == ConfigResult::Applied && c.speedDps == 12.5f);
    for (const char* bad : {"-1", "3001", "1.5"})
        assert(applyConfigKey(c, "servo_delay", bad) == ConfigResult::Invalid);
    for (const char* bad : {"0", "61"})
        assert(applyConfigKey(c, "servo_dps", bad) == ConfigResult::Invalid);
    // No reserved-pin blocking (team decision 22/11): USB/flash/LCD pins are accepted.
    for (const char* pin : {"19", "20", "11", "30", "43"})
        assert(applyConfigKey(c, "servo_pin", pin) == ConfigResult::Applied);
    assert(applyConfigKey(c, "servo_pin", "49") == ConfigResult::Invalid);
    assert(applyConfigKey(c, "servo_pin", "21") == ConfigResult::Applied && c.servoPin == 21);
    // Hard travel 45..135 (22/11): bottom/top/level outside are rejected.
    assert(applyConfigKey(c, "servo_bottom", "50") == ConfigResult::Applied && c.bottomDeg == 50);
    assert(applyConfigKey(c, "servo_top", "130") == ConfigResult::Applied && c.topDeg == 130);
    for (const char* k : {"servo_bottom", "servo_top", "servo_level"})
        for (const char* bad : {"44.9", "135.1", "0", "180"})
            assert(applyConfigKey(c, k, bad) == ConfigResult::Invalid);
    assert(applyConfigKey(c, "servo_bottom", "45") == ConfigResult::Applied &&
           applyConfigKey(c, "servo_top", "135") == ConfigResult::Applied);
    {
        EdgeConfig wide;
        wide.bottomDeg = 0;  // e.g. an old NVS value -> defaults are restored on boot
        assert(!configValid(wide));
    }
    assert(applyConfigKey(c, "servo_step", "4.5") == ConfigResult::Applied && c.stepDeg == 4.5f);
    assert(applyConfigKey(c, "servo_level", "95") == ConfigResult::Applied && c.levelDeg == 95);
    assert(applyConfigKey(c, "servo_dir", "-1") == ConfigResult::Applied && c.dir == -1);
    assert(applyConfigKey(c, "servo_settle", "250") == ConfigResult::Applied && c.settleMs == 250);
    assert(applyConfigKey(c, "hold_sec", "15") == ConfigResult::Applied && c.holdMs() == 15000);
    assert(applyConfigKey(c, "height", "105.5") == ConfigResult::Applied && c.cameraHeightCm == 105.5f);
    assert(applyConfigKey(c, "focal", "1.25") == ConfigResult::Applied && c.focal == 1.25f);
    // Keys of other firmwares are ignored, not errors (same usb_viewer for every board).
    for (const char* k : {"ssid", "password", "camera", "mode", "distance", "reference", "tilt"})
        assert(applyConfigKey(c, k, "x") == ConfigResult::Ignored);
    // Scale MAC (Plan 2 scaleMac, wire key "mac"): normalized to upper case, empty = build default.
    assert(c.scaleMac[0] == '\0');
    assert(applyConfigKey(c, "mac", "70:87:9e:ea:3a:e0") == ConfigResult::Applied);
    assert(std::strcmp(c.scaleMac, "70:87:9E:EA:3A:E0") == 0);
    for (const char* bad : {"", "x", "70:87:9e:ea:3a", "70:87:9e:ea:3a:e0:11", "70-87-9e-ea-3a-e0",
                            "70:87:9g:ea:3a:e0", "7087:9e:ea:3a:e0:"})
        assert(applyConfigKey(c, "mac", bad) == ConfigResult::Invalid);
    assert(std::strcmp(c.scaleMac, "70:87:9E:EA:3A:E0") == 0);  // Rejected values change nothing.
    assert(applyConfigKey(c, "focal", "0") == ConfigResult::Ignored && c.focal == 1.25f);
    // Same ranges as camera/usb_protocol.py SERVO_LIMITS.
    // Reserved pins (19, 11, 30, camera UART 9) are no longer blocked: only the range is checked.
    const char* bad[][2] = {{"servo_pin", "49"}, {"servo_pin", "4.5"}, {"servo_pin", "-1"},
                            {"servo_bottom", "-1"}, {"servo_top", "181"}, {"servo_step", "0.4"},
                            {"servo_step", "21"}, {"servo_dir", "0"}, {"servo_dir", "2"},
                            {"servo_settle", "3001"}, {"servo_settle", "1.5"}, {"hold_sec", "121"},
                            {"hold_sec", "abc"}, {"hold_sec", ""}, {"hold_sec", "nan"},
                            {"height", "0"}, {"focal", "25"}};
    for (auto& kv : bad) assert(applyConfigKey(c, kv[0], kv[1], 9, 8) == ConfigResult::Invalid);
    assert(applyConfigKey(c, "servo_pin", "9", 9, 8) == ConfigResult::Applied);
    assert(applyConfigKey(c, "servo_speed", "1") == ConfigResult::Unknown);
    EdgeConfig same;
    same.bottomDeg = same.topDeg = 90;
    assert(!configValid(same) && configValid(EdgeConfig()));
}

static void testGeometryAndFraming() {
    // TV3 convention: tilt_down = dir * (command - level); dir -1 = larger angle looks UP.
    assert(tiltDownDeg(0, 90, -1) == 90 && tiltDownDeg(180, 90, -1) == -90 && tiltDownDeg(120, 90, 1) == 30);
    for (float d : {0.0f, 37.5f, 90.0f, 151.0f})
        for (int dir : {-1, 1}) assert(tiltDownDeg(d, 88, dir) == servoTiltFromDeg(d, 88, dir));
    const float hc = 110, f = 1.33f;
    for (float H : {120.0f, 150.0f, 170.0f, 190.0f}) {
        for (float D : {250.0f, 280.0f, 320.0f}) {
            const float feet = deg(std::atan(hc / D)), head = deg(std::atan((hc - H) / D));
            const float tilt = 0.5f * (feet + head);  // Person centred vertically.
            const float vt = rowFor(head, tilt, f), vb = rowFor(feet, tilt, f);
            const HeightResult r = heightFromBox(vt, vb, tilt, hc, f);
            if (vt < 0 || vb > 1) {
                // Body taller than the vertical FOV (190 cm at 2.5 m spans 41.5 deg > ~41 deg):
                // no servo angle shows head and feet together (Plan 2: stand farther away).
                assert(!r.valid);
                continue;
            }
            assert(r.valid && near(r.heightCm, H, 0.05f) && near(r.distanceM, D / 100, 0.001f));
        }
    }
    // Invalid: feet above the horizon, inverted box, nonsense inputs, out of range.
    assert(!heightFromBox(0.2f, 0.4f, -20, hc, f).valid);
    assert(!heightFromBox(0.6f, 0.4f, 5, hc, f).valid);
    assert(!heightFromBox(NAN, 0.8f, 5, hc, f).valid);
    assert(!heightFromBox(0.1f, 0.9f, 5, 0, f).valid);
    assert(!heightFromBox(0.1f, 0.9f, 5, hc, 0).valid);
    const float m = 0.03f * 1000;
    auto frame = [&](float y1, float y2, float x1 = 400, float x2 = 600) {
        return analyzeFraming(PersonBox{x1, y1, x2, y2, .9f}, 1000, 1000, m);
    };
    assert(frame(100, 900) == FramingStatus::FullBody);
    assert(frame(10, 900) == FramingStatus::TopClipped);
    assert(frame(100, 990) == FramingStatus::BottomClipped);
    assert(frame(0, 1000) == FramingStatus::TooClose);
    assert(frame(100, 900, 5, 300) == FramingStatus::SideClipped);
    assert(frame(500, 400) == FramingStatus::None);
    assert(std::strcmp(framingName(FramingStatus::TooClose), "TooClose") == 0);
    const float vals[] = {171, 169, 300};
    assert(median(vals, 3) == 171 && median(vals, 2) == 170);
}

static void testMessages() {
    BodyMessage b = parseBodyLine("BOX_0.400_0.100_0.600_0.950_88;0.880");
    assert(b.type == BodyMessageType::Box && near(b.y1, 0.1f, 1e-6f) && near(b.y2, 0.95f, 1e-6f) &&
           b.confidence == 88);
    assert(parseBodyLine("box_0_0_1_1_100").type == BodyMessageType::Box);
    assert(parseBodyLine("NOBODY;1.000").type == BodyMessageType::NoBody);
    assert(parseBodyLine("class:MULTI,100").type == BodyMessageType::Multi);
    for (const char* s : {"BOX_0.6_0.1_0.4_0.9_80", "BOX_0.1_0.1_0.9_1.2_80", "BOX_-0.1_0.1_0.5_0.9_80",
                          "BOX_0.1_0.1_0.9_0.9", "BOX_0.1_0.1_0.9_0.9_101", "BOX_0.1_0.1_0.9_0.9_8x",
                          "XBOX_0.1_0.1_0.9_0.9_80", "BOX_nan_0.1_0.9_0.9_80", "FACE_0.1_80", "", "NOBODYX"})
        assert(parseBodyLine(s).type == BodyMessageType::Invalid);
    assert(parseBodyLine(nullptr).type == BodyMessageType::Invalid);
}

static std::string row(const LcdLines& l, int r) { return std::string(l.line[r]); }

static void testLcdText() {
    LcdData none;
    auto check = [](const LcdLines& l, const char* a, const char* b) {
        assert(std::strlen(l.line[0]) == 16 && std::strlen(l.line[1]) == 16);
        assert(row(l, 0).rfind(a, 0) == 0 && row(l, 1).rfind(b, 0) == 0);
    };
    check(lcdTextFor(SystemPhase::Standby, EdgeNotice::None, none), "Standby", "Step on scale");
    check(lcdTextFor(SystemPhase::WakingHoming, EdgeNotice::None, none), "Waking up...", "Please wait");
    check(lcdTextFor(SystemPhase::ServoSweep, EdgeNotice::None, none), "Aligning cam...", "Stand still");
    check(lcdTextFor(SystemPhase::LockedMeasure, EdgeNotice::None, none), "Measuring...", "Stand still");
    check(lcdTextFor(SystemPhase::ServoSweep, EdgeNotice::TooClose, none), "Step back!", "Too close to cam");
    check(lcdTextFor(SystemPhase::LockedMeasure, EdgeNotice::Multiple, none), "One at a time", "2+ people seen");
    check(lcdTextFor(SystemPhase::ServoSweep, EdgeNotice::NoFullBody, none), "No full body", "Stand straight");
    check(lcdTextFor(SystemPhase::ServoSweep, EdgeNotice::CameraError, none), "Camera error", "Check laptop/IP");
    LcdData r;
    r.hasHeight = true; r.heightCm = 170.24f; r.distanceM = 2.83f;
    check(lcdTextFor(SystemPhase::LockedMeasure, EdgeNotice::None, r), "H:170.2cm", "Wait scale lock");
    r.hasWeight = true; r.weightKg = 65.31f;
    check(lcdTextFor(SystemPhase::LockedMeasure, EdgeNotice::None, r), "H:170.2cm", "W:65.3kg wait");
    check(lcdTextFor(SystemPhase::ResultHold, EdgeNotice::None, r), "H:170.2cm", "W:65.3 unlocked");
    r.weightStable = true; r.hasBmi = true; r.bmi = 22.53f;
    LcdLines res = lcdTextFor(SystemPhase::ResultHold, EdgeNotice::None, r);
    assert(row(res, 0) == "H:170.2 W:65.3  " && row(res, 1) == "BMI:22.5 D:2.8m ");
    // Result is not replaced by a late warning.
    assert(row(lcdTextFor(SystemPhase::ResultHold, EdgeNotice::CameraError, r), 1) == "BMI:22.5 D:2.8m ");
    r.hasWeight = false; r.hasBmi = false;
    check(lcdTextFor(SystemPhase::ResultHold, EdgeNotice::None, r), "H:170.2cm", "No weight lock");
    // Extreme numbers never overflow 16 columns.
    for (float h : {0.0f, 99.9f, 250.0f, 1e6f}) for (float w : {0.0f, 150.0f, 400.0f, 1e6f})
        for (float bmi : {0.0f, 99.95f, 150.0f}) for (float d : {0.0f, 20.0f, 1e6f})
            for (SystemPhase p : {SystemPhase::LockedMeasure, SystemPhase::ResultHold}) {
                LcdData x; x.hasHeight = x.hasWeight = x.hasBmi = x.weightStable = true;
                x.heightCm = h; x.weightKg = w; x.bmi = bmi; x.distanceM = d;
                const LcdLines l = lcdTextFor(p, EdgeNotice::None, x);
                assert(std::strlen(l.line[0]) == 16 && std::strlen(l.line[1]) == 16);
                x.hasBmi = false;
                const LcdLines l2 = lcdTextFor(p, EdgeNotice::None, x);
                assert(std::strlen(l2.line[0]) == 16 && std::strlen(l2.line[1]) == 16);
            }
    // No full-width line got cut mid-word: the long fixed texts are all <= 16.
    assert(std::strlen("Too close to cam") == 16 && std::strlen("Aligning cam...") <= 16);
}

static void testUsbPacket() {
    assert(ssv1Crc32((const uint8_t*)"123456789", 9) == 0xCBF43926u);  // Standard CRC-32 check value.
    uint8_t buf[64];
    const char* json = "{\"kind\":\"config\",\"ok\":true}";
    const size_t n = ssv1Packet(json, buf, sizeof(buf));
    assert(n == 16 + std::strlen(json));
    assert(std::memcmp(buf, "SSV1", 4) == 0 && buf[4] == std::strlen(json) && buf[5] == 0 && buf[8] == 0);
    const uint32_t crc = buf[12] | (buf[13] << 8) | (buf[14] << 16) | ((uint32_t)buf[15] << 24);
    assert(crc == ssv1Crc32((const uint8_t*)json, std::strlen(json)));
    assert(ssv1Packet(json, buf, 20) == 0 && ssv1Packet("", buf, sizeof(buf)) == 0);
}

// Plan 2 Interface Contract: exact signatures (compile-time) + servo -> tilt conversion.
static void testContract() {
    static_assert(std::is_same<decltype(&servoInit), bool (*)(int, int, int)>::value, "servoInit");
    static_assert(std::is_same<decltype(&servoMoveDeg), void (*)(float, uint32_t)>::value, "servoMoveDeg");
    static_assert(std::is_same<decltype(&servoDetach), void (*)()>::value, "servoDetach");
    static_assert(std::is_same<decltype(&servoToTiltDeg), float (*)(float, const EdgeConfig&)>::value,
                  "servoToTiltDeg");
    static_assert(std::is_same<decltype(&lcdSetBacklight), void (*)(bool)>::value, "lcdSetBacklight");
    static_assert(std::is_same<decltype(&analyzeFraming),
                               FramingStatus (*)(const PersonBox&, int, int, float)>::value, "analyzeFraming");

    EdgeConfig c;  // servo_level 90, servo_dir -1 (larger angle looks up)
    assert(std::fabs(servoToTiltDeg(90, c)) < 1e-6f);   // Level.
    assert(std::fabs(servoToTiltDeg(60, c) - 30) < 1e-6f);  // 30 deg below the horizon.
    c.dir = 1;
    c.levelDeg = 100;
    assert(std::fabs(servoToTiltDeg(130, c) - 30) < 1e-6f);  // Reversed mounting.
    EdgeFsm f(c);
    f.begin(0);
    assert(std::fabs(f.tiltDeg() - servoToTiltDeg(f.servoCommand(), c)) < 1e-6f);
}

int main() {
    testContract();
    testConfig();
    testGeometryAndFraming();
    testMessages();
    testLcdText();
    testUsbPacket();
    std::puts("edge_sweep_logic_test: OK");
    return 0;
}
