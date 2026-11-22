// Shared simulator for tests/edge_sweep_fsm_test.cpp and tests/qa_log_sim.cpp:
// scale + servo + camera + laptop (frames every 200 ms, captured 250 ms before they arrive).
#pragma once
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <utility>

#include "edge_fsm.h"

struct Sim {
    static constexpr uint32_t TICK = 20, FRAME = 200, LATENCY = 250;

    EdgeConfig cfg;
    EdgeFsm fsm;
    uint32_t now;
    // World
    float headCm = 170, distCm = 280;  // person on the scale; <= 0 head = nobody in view
    bool onScale = false, multi = false, cameraOn = true;
    float kg = 65, stableAfterMs = 1500;
    uint32_t onScaleAt = 0, ticks = 0;
    std::deque<std::pair<uint32_t, float>> history;  // (time, servo command)
    int moves = 0;
    float lastCmd;
    uint32_t lastMoveAt = 0, minMoveGap = 0xFFFFFFFFu;
    std::function<void(const Sim&)> afterTick;  // Optional hook (log writer in qa_log_sim).

    explicit Sim(EdgeConfig c = EdgeConfig(), uint32_t start = 1000) : cfg(c), fsm(c), now(start) {
        fsm.begin(now);
        lastCmd = fsm.servoCommand();
    }
    void stepOn(float head, float dist) { headCm = head; distCm = dist; onScale = true; onScaleAt = now; }
    void stepOff() { onScale = false; }

    float commandAt(uint32_t t) const {
        float c = history.empty() ? lastCmd : history.front().second;
        for (const auto& h : history) if ((int32_t)(h.first - t) <= 0) c = h.second;
        return c;
    }

    BodyMessage frameMessage() const {
        using namespace edge_geometry;
        BodyMessage m;
        if (multi) { m.type = BodyMessageType::Multi; return m; }
        m.type = BodyMessageType::NoBody;
        if (!onScale || headCm <= 0) return m;
        const float tilt = tiltDownDeg(commandAt(now - LATENCY), cfg.levelDeg, cfg.dir);
        const float feet = deg(std::atan(cfg.cameraHeightCm / distCm));
        const float head = deg(std::atan((cfg.cameraHeightCm - headCm) / distCm));
        // Rays more than 90 deg from the optical axis are behind the camera.
        if (std::fabs(feet - tilt) >= 89 || std::fabs(head - tilt) >= 89) return m;
        float vt = 0.5f + cfg.focal * std::tan(rad(head - tilt));
        float vb = 0.5f + cfg.focal * std::tan(rad(feet - tilt));
        if (vb <= 0 || vt >= 1) return m;  // Whole body outside the image.
        m.type = BodyMessageType::Box;
        m.x1 = 0.42f; m.x2 = 0.58f;
        m.y1 = vt < 0 ? 0 : vt;
        m.y2 = vb > 1 ? 1 : vb;
        m.confidence = 90;
        return m;
    }

    void run(uint32_t ms) {
        for (uint32_t t = 0; t < ms; t += TICK) {
            now += TICK;
            const bool stable = onScale && now - onScaleAt >= stableAfterMs;
            fsm.setScale(now, onScale, onScale ? kg : 0, stable);
            if (cameraOn && ++ticks % (FRAME / TICK) == 0) fsm.onFrame(now, frameMessage());
            fsm.update(now);
            const float c = fsm.servoCommand();
            if (c != lastCmd) {
                if (moves && fsm.phase() == SystemPhase::ServoSweep) {
                    const uint32_t gap = now - lastMoveAt;
                    if (gap < minMoveGap) minMoveGap = gap;
                }
                ++moves; lastCmd = c; lastMoveAt = now;
            }
            history.emplace_back(now, c);
            while (history.size() > 64) history.pop_front();
            if (afterTick) afterTick(*this);
        }
    }
    long until(SystemPhase p, uint32_t maxMs) {
        for (uint32_t t = 0; t <= maxMs; t += TICK) {
            if (fsm.phase() == p) return (long)t;
            run(TICK);
        }
        return -1;
    }
    long untilNotice(EdgeNotice n, uint32_t maxMs) {
        for (uint32_t t = 0; t <= maxMs; t += TICK) {
            if (fsm.notice() == n) return (long)t;
            run(TICK);
        }
        return -1;
    }
};
