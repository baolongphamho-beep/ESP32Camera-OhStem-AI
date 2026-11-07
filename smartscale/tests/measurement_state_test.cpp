#include <cassert>
#include <cmath>
#include "measurement_state.h"
#include "camera_packet.h"

int main() {
    MeasurementState state;
    assert(state.view(0).status == PersonStatus::None);
    state.camera(1000, PersonStatus::Measured, 180, 3);
    assert(state.view(1000).hasHeight);
    assert(!state.view(1000).hasBMI);
    state.weight(1100, 81, false);
    assert(!state.view(1100).hasBMI);  // Provisional weight is not a BMI result.
    state.weight(1200, 81, true);
    assert(std::fabs(state.view(1200).bmi - 25) < 0.001f);
    state.camera(1300, PersonStatus::None);
    assert(!state.view(1300).hasHeight && !state.view(1300).hasBMI);
    state.camera(1400, PersonStatus::Measured, 160, 3);
    assert(!state.view(1400).hasWeight);  // No weight carried across absence.
    state.weight(1500, 64, true);
    state.camera(1600, PersonStatus::Partial);
    assert(!state.view(1600).hasBMI);
    state.camera(1700, PersonStatus::Measured, 160, 3);
    assert(state.view(1700).hasBMI);
    assert(!state.view(4800).hasBMI);  // Camera TTL.
    state.camera(5000, PersonStatus::Measured, 170, 3);
    assert(!state.view(5000).hasWeight); // New camera session, old weight discarded.
    state.weight(5100, 80, true);
    state.camera(5200, PersonStatus::Multiple);
    assert(!state.view(5200).hasWeight);
    state.camera(36000, PersonStatus::Measured, 170, 3);
    assert(!state.view(36000).hasBMI);
    state.weight(36001, 80, true);
    state.clearWeight();
    assert(!state.view(36002).hasWeight);

    // Millisecond counter rollover must not expire a newly received packet.
    state.camera(0xfffffff0U, PersonStatus::Measured, 180, 3);
    state.weight(0xfffffff5U, 81, true);
    assert(state.view(20).hasBMI);

    PersonStatus status;
    float height, distance;
    assert(decodeCameraLabel("MEAS_H180.0_D3.000", status, height, distance));
    assert(status == PersonStatus::Measured && height == 180 && distance == 3);
    assert(decodeCameraLabel("NO_PEOPLE", status, height, distance));
    assert(status == PersonStatus::None);
    assert(!decodeCameraLabel("MEAS_H180_Dnan", status, height, distance));
    assert(!decodeCameraLabel("MEAS_H180_D3junk", status, height, distance));
    assert(!decodeCameraLabel("MEAS_H0_D3", status, height, distance));
    assert(!decodeCameraLabel("Face", status, height, distance));
    return 0;
}
