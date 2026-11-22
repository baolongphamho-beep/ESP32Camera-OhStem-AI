#pragma once
// Project 3 pedestrian warning (pedestrian_distance.py -> camera -> UART -> LCD).
// Pure logic, no Arduino: tested by tests/pedestrian_state_test.cpp.
//
// Labels (letters, digits, '_', '.', no ';'):
//   PED_WARN_D1.80   person closer than the laptop's SAFE_DISTANCE, 1.80 m away
//   PED_SAFE_D3.20   person farther than SAFE_DISTANCE
//   NO_PEOPLE        nobody (already handled by camera_packet.h; also clears this state)
// The threshold lives in the laptop script so it can change without reflashing.
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#ifndef PEDESTRIAN_TTL_MS
#define PEDESTRIAN_TTL_MS 2000  // Script sends ~2/s; older data is dropped from the LCD.
#endif

struct PedestrianView {
    bool active = false;
    bool warning = false;
    float distanceM = 0;
};

inline bool decodePedestrianLabel(const char* label, float& distanceM, bool& warning) {
    if (!label) return false;
    const char* number = nullptr;
    if (std::strncmp(label, "PED_WARN_D", 10) == 0) { warning = true; number = label + 10; }
    else if (std::strncmp(label, "PED_SAFE_D", 10) == 0) { warning = false; number = label + 10; }
    else return false;
    if (!(*number >= '0' && *number <= '9')) return false;
    char* end = nullptr;
    const float d = std::strtof(number, &end);
    if (end == number || *end != '\0' || !std::isfinite(d) || d <= 0 || d > 50) return false;
    distanceM = d;
    return true;
}

class PedestrianState {
public:
    void update(uint32_t now, float distanceM, bool warning) {
        at_ = now;
        distance_ = distanceM;
        warning_ = warning;
        valid_ = true;
    }
    void clear() { valid_ = false; }
    PedestrianView view(uint32_t now) const {
        PedestrianView out;
        if (!valid_ || now - at_ > PEDESTRIAN_TTL_MS) return out;
        out.active = true;
        out.warning = warning_;
        out.distanceM = distance_;
        return out;
    }

private:
    uint32_t at_ = 0;
    float distance_ = 0;
    bool warning_ = false, valid_ = false;
};
