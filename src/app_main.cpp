// Plan 2 servo sweep: step on the Mi Scale 2 -> the servo tilts the camera from
// servo_bottom to servo_top until the whole body is in the frame -> height from the
// servo tilt + person box -> weight from the scale -> BMI -> LCD (per phase) and
// result hold. Compiled only by env:yolo_uno_servo_sweep; empty in every other env.
// Named after Plan 2's firmware/edge_yolo/main/app_main.cpp (TV1: FSM + integration);
// modules follow the Interface Contract: servo_ctrl.h (3.2), edge_io.h (3.3, LCD part),
// edge_geometry.h (3.4), edge_config.h = EdgeConfig + NVS keys (3.5). All in lib/EdgeSweep.
//
// Laptop (vision/body_tracker.py) -> camera /control -> camera UART -> this board:
//   BOX_<x1>_<y1>_<x2>_<y2>_<conf>  |  NOBODY  |  MULTI     (lib/EdgeSweep/edge_message.h)
// USB Serial (115200):
//   BEGIN / CFG key=value / APPLY   from camera/usb_viewer.py --configure / --set (saved in NVS)
//   reset | status | config | config defaults | BOX_... | NOBODY | MULTI (bench test)
// Every second a binary SSV1 "status" packet is sent for usb_viewer.py (phase, servo_deg,
// tilt_deg, framing, H/W/BMI); Serial Monitor shows it as a short garbled line.
#ifdef SERVO_SWEEP_MODE
#include <Arduino.h>
#include <HardwareSerial.h>
#include <Preferences.h>
#include <strings.h>

#include "config.h"
#include "display.h"
#include "edge_fsm.h"
#include "edge_io.h"
#include "measurements.h"
#include "scale.h"
#include "servo_ctrl.h"
#include "usb_packet.h"

#ifndef SWEEP_USB_STATUS
#define SWEEP_USB_STATUS 1
#endif

static EdgeConfig cfg;
static EdgeFsm fsm;
static HardwareSerial cameraSerial(SWEEP_CAMERA_UART);
static Preferences prefs;
static uint32_t lastCameraLineMs = 0;

// ---------------------------------------------------------------- config (NVS)

static void loadConfig() {
    prefs.begin("sweep_cfg", true);
    cfg.servoPin = prefs.getInt("servo_pin", cfg.servoPin);
    cfg.bottomDeg = prefs.getFloat("servo_bottom", cfg.bottomDeg);
    cfg.topDeg = prefs.getFloat("servo_top", cfg.topDeg);
    cfg.stepDeg = prefs.getFloat("servo_step", cfg.stepDeg);
    cfg.levelDeg = prefs.getFloat("servo_level", cfg.levelDeg);
    // Same NVS types as TV3 servo_ctrl.zip (putInt = i32, putUInt = u32, putFloat = blob).
    // Before 22/11 servo_dir used tilt = dir * (level - command); "dir_tv3" marks the
    // TV3 sign, so an older saved value is inverted once instead of tilting backwards.
    const bool tv3Sign = prefs.getBool("dir_tv3", false);
    if (prefs.isKey("servo_dir")) {
        const int saved = prefs.getInt("servo_dir", cfg.dir);
        cfg.dir = tv3Sign ? saved : -saved;
    }
    cfg.settleMs = prefs.getUInt("servo_settle", cfg.settleMs);
    cfg.frameDelayMs = prefs.getUInt("servo_delay", cfg.frameDelayMs);
    cfg.speedDps = prefs.getFloat("servo_dps", cfg.speedDps);
    cfg.holdSec = prefs.getFloat("hold_sec", cfg.holdSec);
    cfg.cameraHeightCm = prefs.getFloat("height", cfg.cameraHeightCm);
    cfg.focal = prefs.getFloat("focal", cfg.focal);
    const String mac = prefs.getString("scale_mac", "");
    prefs.end();
    if (!configValid(cfg)) {
        Serial.println("[CFG] Saved settings invalid -> using defaults");
        cfg = EdgeConfig();
    }
    char normalized[18];
    if (mac.length() && sweep_config_detail::normalizeMac(mac.c_str(), normalized))
        memcpy(cfg.scaleMac, normalized, sizeof(normalized));
    if (cfg.scaleMac[0]) setScaleMac(cfg.scaleMac);  // Before the BLE scan task starts.
}

static void saveConfig() {
    prefs.begin("sweep_cfg", false);
    prefs.putInt("servo_pin", cfg.servoPin);
    prefs.putFloat("servo_bottom", cfg.bottomDeg);
    prefs.putFloat("servo_top", cfg.topDeg);
    prefs.putFloat("servo_step", cfg.stepDeg);
    prefs.putFloat("servo_level", cfg.levelDeg);
    prefs.putInt("servo_dir", cfg.dir);
    prefs.putBool("dir_tv3", true);
    prefs.putUInt("servo_settle", cfg.settleMs);
    prefs.putUInt("servo_delay", cfg.frameDelayMs);
    prefs.putFloat("servo_dps", cfg.speedDps);
    prefs.putFloat("hold_sec", cfg.holdSec);
    prefs.putFloat("height", cfg.cameraHeightCm);
    prefs.putFloat("focal", cfg.focal);
    prefs.putString("scale_mac", cfg.scaleMac);
    prefs.end();
}

static void printConfig() {
    Serial.printf("[CFG] servo_pin=%d servo_bottom=%.1f servo_top=%.1f servo_step=%.1f servo_level=%.1f "
                  "servo_dir=%d servo_settle=%lu servo_delay=%lu servo_dps=%.1f hold_sec=%.1f height=%.1f "
                  "focal=%.3f mac=%s%s\n",
                  cfg.servoPin, cfg.bottomDeg, cfg.topDeg, cfg.stepDeg, cfg.levelDeg, cfg.dir,
                  (unsigned long)cfg.settleMs, (unsigned long)cfg.frameDelayMs, cfg.speedDps, cfg.holdSec, cfg.cameraHeightCm, cfg.focal, scaleMac(),
                  cfg.scaleMac[0] ? "" : " (build default SCALE_MAC)");
}

static void sendPacket(const char* json) {
#if SWEEP_USB_STATUS
    static uint8_t buffer[16 + 640];
    const size_t n = ssv1Packet(json, buffer, sizeof(buffer));
    if (n) Serial.write(buffer, n);
#else
    (void)json;
#endif
}

static void applyConfig(const EdgeConfig& next) {
    cfg = next;
    servoDetach();  // Pin or travel limits may have changed: driveServo() re-inits if needed.
    fsm.setConfig(cfg);
    setResultHoldMs(cfg.holdMs());
}

// BEGIN / CFG key=value / APPLY batch: all keys valid -> applied + saved, else nothing.
static struct {
    bool open = false, ok = true;
    EdgeConfig pending;
    char ignored[96] = "";
    char error[48] = "";
} batch;

static void handleConfigLine(const char* line) {
    if (!strcmp(line, "BEGIN")) {
        batch.open = true;
        batch.ok = true;
        batch.pending = cfg;
        batch.ignored[0] = batch.error[0] = '\0';
        return;
    }
    if (!batch.open) return;
    if (!strcmp(line, "APPLY")) {
        batch.open = false;
        char json[200];
        if (batch.ok && !configValid(batch.pending)) {
            batch.ok = false;
            snprintf(batch.error, sizeof(batch.error), "servo_bottom == servo_top");
        }
        // The BLE scan task reads the MAC only at start -> reboot when it changes.
        const bool macChanged = batch.ok && batch.pending.scaleMac[0] &&
                                strcasecmp(batch.pending.scaleMac, scaleMac()) != 0;
        if (batch.ok) {
            applyConfig(batch.pending);
            saveConfig();
            Serial.println("[CFG] Applied and saved");
            printConfig();
        } else {
            Serial.printf("[CFG] Rejected (%s); nothing changed\n", batch.error);
        }
        snprintf(json, sizeof(json), "{\"kind\":\"config\",\"ok\":%s,\"ignored\":\"%s\",\"error\":\"%s\"}",
                 batch.ok ? "true" : "false", batch.ignored, batch.error);
        sendPacket(json);
        if (macChanged) {
            Serial.printf("[CFG] Scale MAC -> %s: rebooting in 1 s to rescan\n", batch.pending.scaleMac);
            Serial.flush();
            delay(1000);
            ESP.restart();
        }
        return;
    }
    if (strncmp(line, "CFG ", 4) != 0) return;
    char key[24], value[170];
    const char* eq = strchr(line + 4, '=');
    if (!eq || eq - (line + 4) >= (int)sizeof(key)) { batch.ok = false; return; }
    const size_t keyLen = (size_t)(eq - (line + 4));
    memcpy(key, line + 4, keyLen);
    key[keyLen] = '\0';
    snprintf(value, sizeof(value), "%s", eq + 1);
    const ConfigResult r = applyConfigKey(batch.pending, key, value, SWEEP_CAMERA_RX_PIN, SWEEP_CAMERA_TX_PIN);
    if (r == ConfigResult::Ignored) {
        if (strlen(batch.ignored) + strlen(key) + 2 < sizeof(batch.ignored)) {
            if (batch.ignored[0]) strcat(batch.ignored, ",");
            strcat(batch.ignored, key);
        }
    } else if (r != ConfigResult::Applied) {
        batch.ok = false;
        snprintf(batch.error, sizeof(batch.error), "%s %s", r == ConfigResult::Unknown ? "unknown" : "invalid", key);
    }
}

// ---------------------------------------------------------------- servo

// The FSM decides where and when; servo_ctrl (Plan 2 contract 3.2) drives the pin.
static void driveServo() {
    if (!fsm.servoActive()) {
        if (servoAttached()) servoDetach();
        return;
    }
    if (!servoAttached()) {
        const int lo = (int)floorf(cfg.bottomDeg < cfg.topDeg ? cfg.bottomDeg : cfg.topDeg);
        const int hi = (int)ceilf(cfg.bottomDeg < cfg.topDeg ? cfg.topDeg : cfg.bottomDeg);
        if (!servoInit(cfg.servoPin, lo, hi)) {
            static uint32_t lastWarn = 0;
            if (millis() - lastWarn > 5000) {
                lastWarn = millis();
                Serial.printf("[SERVO ERROR] Cannot attach servo on GPIO%d\n", cfg.servoPin);
            }
            return;
        }
    }
    servoMoveDeg(fsm.servoCommand(), cfg.settleMs);  // No-op when the command did not change.
}

// ---------------------------------------------------------------- input lines

static void handleFrame(const char* line, uint32_t now, bool fromCamera) {
    const BodyMessage m = parseBodyLine(line);
    if (m.type == BodyMessageType::Invalid) {
        if (fromCamera) Serial.printf("[CAMERA] Ignored: %s\n", line);
        return;
    }
    fsm.onFrame(now, m);
}

static void handleSerialLine(const char* line) {
    const uint32_t now = millis();
    if (!strcmp(line, "BEGIN") || !strcmp(line, "APPLY") || !strncmp(line, "CFG ", 4)) {
        handleConfigLine(line);
    } else if (!strcasecmp(line, "reset")) {
        fsm.reset(now);
        clearResultHold();
        Serial.println("[SWEEP] Reset -> STANDBY");
    } else if (!strcasecmp(line, "config")) {
        printConfig();
    } else if (!strcasecmp(line, "config defaults")) {
        prefs.begin("sweep_cfg", false);
        prefs.clear();
        prefs.end();
        applyConfig(EdgeConfig());
        Serial.println("[CFG] Defaults restored (scale MAC back to SCALE_MAC after the next reboot)");
        printConfig();
    } else if (!strcasecmp(line, "status")) {
        Serial.printf("[SWEEP] %s servo=%.1f tilt=%.1f framing=%s notice=%s H=%s camera=%s\n",
                      phaseName(fsm.phase()), fsm.servoCommand(), fsm.tiltDeg(), framingName(fsm.framing()),
                      noticeName(fsm.notice()), fsm.hasHeight() ? String(fsm.heightCm(), 1).c_str() : "--",
                      lastCameraLineMs ? (String(now - lastCameraLineMs) + " ms ago").c_str() : "never");
    } else if (parseBodyLine(line).type != BodyMessageType::Invalid) {
        handleFrame(line, now, false);  // Bench test without the laptop.
    } else {
        Serial.println("[CMD] reset | status | config | config defaults | BOX_x1_y1_x2_y2_conf | NOBODY | MULTI");
    }
}

static void pollLines(Stream& in, char* buf, size_t cap, size_t& used, uint32_t& lastByte, bool camera) {
    const uint32_t now = millis();
    for (int count = 0; count < 128 && in.available() > 0; ++count) {
        const char c = (char)in.read();
        lastByte = now;
        if (c == '\n' || c == '\r') {
            if (used) {
                buf[used] = '\0';
                if (camera) { lastCameraLineMs = now; handleFrame(buf, now, true); }
                else handleSerialLine(buf);
            }
            used = 0;
        } else if (used < cap - 1) {
            buf[used++] = c;
        }
    }
    // The camera may send a frame without a newline.
    if (camera && used && now - lastByte > 50) {
        buf[used] = '\0';
        lastCameraLineMs = now;
        handleFrame(buf, now, true);
        used = 0;
    }
}

// ---------------------------------------------------------------- setup / loop

void setup() {
    Serial.begin(115200);
    const uint32_t start = millis();
    while (!Serial && millis() - start < 3000) delay(10);
    Serial.println("[BOOT] Plan 2 servo sweep + scale + BMI");
    delay(500);  // LCD backpack supply settle.

    loadConfig();
    initDisplay();
    cameraSerial.setRxBufferSize(512);
    cameraSerial.begin(115200, SERIAL_8N1, SWEEP_CAMERA_RX_PIN, SWEEP_CAMERA_TX_PIN);
    fsm.setConfig(cfg);
    fsm.begin(millis());
    setResultHoldMs(cfg.holdMs());
    if (xTaskCreate(runScaleService, "ScaleBLEService", 10240, NULL, 1, NULL) != pdPASS) {
        Serial.println("[BOOT ERROR] Cannot create scale task");
    }
    Serial.printf("[BOOT] Camera UART RX=%d TX=%d, servo pulse %d-%d us\n", SWEEP_CAMERA_RX_PIN,
                  SWEEP_CAMERA_TX_PIN, SWEEP_SERVO_MIN_US, SWEEP_SERVO_MAX_US);
    printConfig();
    Serial.println("[SYSTEM] Ready (STANDBY: step on the scale)");
}

void loop() {
    static char camBuf[160], usbBuf[200];
    static size_t camUsed = 0, usbUsed = 0;
    static uint32_t camByte = 0, usbByte = 0;
    pollLines(cameraSerial, camBuf, sizeof(camBuf), camUsed, camByte, true);
    pollLines(Serial, usbBuf, sizeof(usbBuf), usbUsed, usbByte, false);

    const uint32_t now = millis();
    MeasurementView view = measurementView();
    fsm.setScale(now, view.hasWeight, view.weightKg, view.weightStable);
    fsm.update(now);
    driveServo();

    // Shared measurement state: height from the sweep, weight from the scale task.
    // "Partial" keeps the scale weight alive before a height exists.
    static uint32_t lastPublish = 0;
    if (now - lastPublish >= 500) {
        lastPublish = now;
        if (fsm.hasHeight()) updateCameraMeasurement(PersonStatus::Measured, fsm.heightCm(), fsm.distanceM());
        else updateCameraMeasurement(PersonStatus::Partial);
        view = measurementView();
    }

    // LCD: live result first, then the held one (TV5-1) after stepping off.
    const ResultHold held = heldResult();
    LcdData d;
    d.hasHeight = fsm.hasHeight();
    d.heightCm = fsm.heightCm();
    d.distanceM = fsm.distanceM();
    if (view.hasBMI) {
        d.hasWeight = d.weightStable = d.hasBmi = true;
        d.weightKg = view.weightKg;
        d.bmi = view.bmi;
    } else if (held.active && fsm.phase() == SystemPhase::ResultHold) {
        d.hasWeight = d.weightStable = d.hasBmi = true;
        d.weightKg = held.weightKg;
        d.bmi = held.bmi;
    } else {
        d.hasWeight = view.hasWeight;
        d.weightKg = view.weightKg;
        d.weightStable = view.weightStable;
    }
    lcdSetBacklight(fsm.phase() != SystemPhase::Standby);  // Dark in STANDBY (Plan 2).
    const LcdLines lines = lcdShowPhase(fsm.phase(), fsm.notice(), d);

    // "ms=" timestamps are read by tools/qa_log_check.py (TV5-5 QA timing).
    static bool lastPresent = false;
    if (fsm.present() != lastPresent) {
        lastPresent = fsm.present();
        Serial.printf("[SCALE] %s kg=%.2f ms=%lu\n", lastPresent ? "on" : "off", view.weightKg,
                      (unsigned long)now);
    }

    static SystemPhase lastPhase = SystemPhase::Standby;
    static EdgeNotice lastNotice = EdgeNotice::None;
    if (fsm.phase() != lastPhase || fsm.notice() != lastNotice) {
        lastPhase = fsm.phase();
        lastNotice = fsm.notice();
        Serial.printf("[SWEEP] -> %s%s%s servo=%.1f tilt=%.1f ms=%lu | LCD \"%s\" / \"%s\"\n",
                      phaseName(lastPhase), lastNotice != EdgeNotice::None ? " notice=" : "",
                      noticeName(lastNotice), fsm.servoCommand(), fsm.tiltDeg(), (unsigned long)now,
                      lines.line[0], lines.line[1]);
    }

    static bool printed = false;
    if (d.hasBmi && fsm.phase() == SystemPhase::ResultHold && !printed) {
        Serial.printf("[RESULT] H=%.1f W=%.2f BMI=%.2f D=%.2f servo=%.1f ms=%lu\n", d.heightCm, d.weightKg,
                      d.bmi, d.distanceM, fsm.servoCommand(), (unsigned long)now);
        Serial.println("========== HEIGHT / WEIGHT / BMI ==========");
        Serial.printf("Height = %.1f cm (servo %.1f deg, tilt %.1f deg, D %.2f m)\n", d.heightCm,
                      fsm.servoCommand(), fsm.tiltDeg(), d.distanceM);
        Serial.printf("Weight = %.2f kg\nBMI    = %.2f\n", d.weightKg, d.bmi);
        Serial.println("===========================================");
    }
    printed = d.hasBmi && fsm.phase() == SystemPhase::ResultHold;

    static uint32_t lastStatus = 0;
    if (now - lastStatus >= 1000) {
        lastStatus = now;
        char json[400];
        snprintf(json, sizeof(json),
                 "{\"kind\":\"status\",\"message\":\"%s | %s\",\"phase\":\"%s\",\"servo_deg\":%.1f,"
                 "\"tilt_deg\":%.1f,\"framing\":\"%s\",\"notice\":\"%s\",\"height_cm\":%s,\"weight_kg\":%s,"
                 "\"bmi\":%s,\"scale\":\"%s\",\"lcd\":\"%s\"}",
                 lines.line[0], lines.line[1], phaseName(fsm.phase()), fsm.servoCommand(), fsm.tiltDeg(),
                 framingName(fsm.framing()), noticeName(fsm.notice()),
                 d.hasHeight ? String(d.heightCm, 1).c_str() : "null",
                 d.hasWeight ? String(d.weightKg, 2).c_str() : "null",
                 d.hasBmi ? String(d.bmi, 2).c_str() : "null", scaleServiceStatus(), displayStatus());
        sendPacket(json);
    }
    delay(5);
}
#endif
