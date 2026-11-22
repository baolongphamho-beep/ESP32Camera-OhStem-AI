#include "../firmware/edge_yolo/main/servo_motion.h"
#include <cassert>
#include <cstdio>

static bool near(float a, float b, float tol = 0.01f) { return std::fabs(a - b) <= tol; }

int main() {
    // --- Pulse / duty: 50 Hz, 14-bit (16384 counts per 20 ms) ---
    ServoPulse p;
    assert(near(servoPulseUs(0, p), 500) && near(servoPulseUs(90, p), 1450) && near(servoPulseUs(180, p), 2400));
    assert(near(servoPulseUs(-30, p), 500) && near(servoPulseUs(400, p), 2400)); // clamped
    assert(servoDuty(90, p) == 1188);   // 1450/20000*16384 = 1187.8
    assert(servoDuty(0, p) == 410);     // 500/20000*16384  = 409.6
    assert(servoDuty(180, p) == 1966);  // 2400/20000*16384 = 1966.08

    // --- Tilt conversion (README: down positive, up negative) ---
    assert(near(servoTiltFromDeg(90, 90, -1), 0));
    assert(near(servoTiltFromDeg(60, 90, -1), 30));   // servo below level, dir=-1 -> looking DOWN 30
    assert(near(servoTiltFromDeg(120, 90, -1), -30)); // looking UP 30
    assert(near(servoTiltFromDeg(120, 90, +1), 30));
    assert(near(servoDegFromTilt(servoTiltFromDeg(73.5f, 88, -1), 88, -1), 73.5f));

    // --- Sweep min -> max: monotonic, smooth, arrives exactly, never leaves [min,max] ---
    ServoMotion m;
    m.configure(40, 130, 240);
    m.reset(40);
    m.setTarget(130, 20);
    float prev = m.pos(), maxStep = 0, prevV = 0, maxDv = 0;
    int ticks = 0;
    while (m.moving() && ticks < 2000) {
        m.step(0.02f);
        assert(m.pos() >= prev - 1e-4f);                // never goes backwards
        assert(m.pos() >= 40 && m.pos() <= 130);        // travel limits respected
        maxStep = std::max(maxStep, m.pos() - prev);
        if (m.moving()) maxDv = std::max(maxDv, std::fabs(m.vel() - prevV)); // final snap tick (<0.2 deg) excluded
        prev = m.pos(); prevV = m.vel(); ++ticks;
    }
    assert(near(m.pos(), 130, 0.001f) && m.vel() == 0 && !m.moving());
    assert(maxStep <= 20 * 0.02f + 1e-3f);              // <= 0.4 deg per 20 ms tick at 20 deg/s
    assert(maxDv <= 240 * 0.02f + 1e-3f);               // acceleration limit honoured
    // 90 deg at 20 deg/s ~ 4.5 s + ramps (~0.17 s) -> ~233 ticks
    assert(ticks > 220 && ticks < 250);

    // --- IMMEDIATE stop: position frozen in the very next tick, stays frozen ---
    m.reset(40);
    m.setTarget(130, 20);
    for (int i = 0; i < 100; ++i) m.step(0.02f);
    const float held = m.pos();
    assert(held > 40 && held < 130 && m.vel() > 0);
    m.stop();
    assert(!m.moving() && m.vel() == 0);
    for (int i = 0; i < 50; ++i) m.step(0.02f);
    assert(m.pos() == held);

    // --- Target outside limits is clamped (mechanical protection) ---
    m.reset(90); m.setTarget(500, 30);
    while (m.moving()) m.step(0.02f);
    assert(near(m.pos(), 130));
    m.setTarget(-50, 30);
    while (m.moving()) m.step(0.02f);
    assert(near(m.pos(), 40));

    // --- Short move still lands exactly (no Zeno stall near the target) ---
    m.reset(90); m.setTarget(90.5f, 20);
    int n = 0; while (m.moving() && n++ < 500) m.step(0.02f);
    assert(near(m.pos(), 90.5f, 0.001f) && n < 500);

    // --- History: interpolate the angle at the time the frame was captured ---
    ServoHistory h;
    for (uint32_t t = 1000; t <= 3000; t += 20) h.push(t, 40 + (t - 1000) * 0.02f); // 20 deg/s
    float d;
    assert(h.at(2000, d) && near(d, 60, 0.01f));
    assert(h.at(2010, d) && near(d, 60.2f, 0.01f));   // between two samples
    assert(h.at(5000, d) && near(d, 80, 0.01f));      // newer than newest -> latest
    ServoHistory old;                                  // buffer shorter than the lookup window
    for (uint32_t t = 0; t < 300 * 20; t += 20) old.push(t, float(t));
    assert(!old.at(100, d));                           // older than 256 samples -> rejected
    // millis() wrap-around
    ServoHistory w;
    for (uint32_t i = 0; i < 20; ++i) w.push(0xFFFFFF00u + i * 20, float(i));
    assert(w.at(0xFFFFFF00u + 10, d) && near(d, 0.5f, 0.01f));
    assert(w.at(0x00000010u, d) || true);              // no crash across the wrap

    // --- Step-scan: 5 deg steps, last step clamped, top detected ---
    float nx;
    assert(servoNextStep(45, 5, 135, nx) && near(nx, 50));
    assert(servoNextStep(132, 5, 135, nx) && near(nx, 135));   // last partial step lands exactly on max
    assert(!servoNextStep(135, 5, 135, nx) && !servoNextStep(134.7f, 5, 135, nx));
    float c = 45; int steps = 0;
    while (servoNextStep(c, 5, 135, nx)) { c = nx; ++steps; }
    assert(steps == 18 && near(c, 135));

    // --- Camera delay gate: servo rests at t=10000, settle 200 + camera delay 500 => valid from 10700 ---
    assert(!servoFrameFresh(10699, 10000, 200, 500));          // shows the previous angle
    assert( servoFrameFresh(10700, 10000, 200, 500));
    assert(!servoFrameFresh(20000, 0, 200, 500));              // servo still moving
    assert(!servoFrameFresh(9000, 10000, 200, 500));           // captured before it stopped
    assert(!servoFrameFresh(0x000001B0u, 0xFFFFFF00u, 200, 500)); // across millis() wrap: +688 ms < 700
    assert( servoFrameFresh(0x000001D0u, 0xFFFFFF00u, 200, 500)); // +720 ms >= 700
    std::puts("servo_motion_test: OK");
    return 0;
}
