// g++ -std=c++17 -I lib/Measurements tests/pedestrian_state_test.cpp -o .pio/pst.exe; .pio/pst.exe
#include <cassert>
#include <cmath>
#include <cstdio>
#include "pedestrian_state.h"
#include "camera_packet.h"

int main() {
    float d = 0;
    bool warn = false;
    assert(decodePedestrianLabel("PED_WARN_D1.80", d, warn) && warn && std::fabs(d - 1.8f) < 1e-5f);
    assert(decodePedestrianLabel("PED_SAFE_D12.5", d, warn) && !warn && std::fabs(d - 12.5f) < 1e-5f);
    const char* bad[] = {"PED_WARN_D", "PED_WARN_D-1", "PED_SAFE_D0", "PED_SAFE_D99", "PED_SAFE_D1.8m",
                         "PED_SAFE_Dnan", "PED_SAFE_Dinf", "PED_STOP_D1.0", "DIST_1.8m", "NO_PEOPLE",
                         "PED_WARN_D 1.0"};
    for (const char* s : bad) assert(!decodePedestrianLabel(s, d, warn));
    assert(!decodePedestrianLabel(nullptr, d, warn));

    // PED_ labels must not be mistaken for P5 labels, and vice versa.
    PersonStatus status;
    float h, dist;
    assert(!decodeCameraLabel("PED_WARN_D1.80", status, h, dist));
    assert(!decodePedestrianLabel("MEAS_H180.0_D3.000", d, warn));

    PedestrianState s;
    assert(!s.view(0).active);
    s.update(1000, 1.8f, true);
    PedestrianView v = s.view(1500);
    assert(v.active && v.warning && std::fabs(v.distanceM - 1.8f) < 1e-5f);
    assert(s.view(1000 + PEDESTRIAN_TTL_MS).active);
    assert(!s.view(1001 + PEDESTRIAN_TTL_MS).active);  // Expired -> LCD back to normal.
    s.update(5000, 3.2f, false);
    assert(s.view(5100).active && !s.view(5100).warning);
    s.clear();                                          // NO_PEOPLE.
    assert(!s.view(5200).active);

    // millis() rollover.
    s.update(0xFFFFFF00U, 2.0f, true);
    assert(s.view(0x00000100U).active);

    std::puts("pedestrian_state_test: OK");
    return 0;
}
