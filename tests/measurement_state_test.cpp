#include <cassert>
#include <cmath>
#include "measurement_state.h"
#include "camera_packet.h"

int main() {
    // Scale-only diagnostics must work before any camera packet arrives.
    MeasurementState scaleOnly;
    assert(!scaleOnly.weightView(0).hasWeight);
    scaleOnly.weight(100, 13.25f, false);
    assert(scaleOnly.weightView(100).hasWeight);
    assert(!scaleOnly.weightView(100).weightStable);
    assert(!scaleOnly.view(100).hasWeight); // Existing camera gating stays intact.
    scaleOnly.weight(200, 13.25f, true);
    assert(scaleOnly.weightView(200).weightStable);
    assert(std::fabs(scaleOnly.weightView(200).weightKg - 13.25f) < 0.001f);
    assert(!scaleOnly.weightView(200).hasBMI);
    assert(scaleOnly.weightView(30200).hasWeight);
    assert(!scaleOnly.weightView(30201).hasWeight);
    scaleOnly.clearWeight();
    assert(!scaleOnly.weightView(201).hasWeight);
    scaleOnly.weight(0xfffffff5U, 13.25f, true);
    assert(scaleOnly.weightView(20).weightStable);

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

    // Optional ResultHold preserves final BMI briefly after user steps off scale.
    MeasurementState heldState(12000);
    heldState.camera(1000, PersonStatus::Measured, 175, 2.5f);
    heldState.weight(1100, 70, true);
    assert(heldState.view(1100).hasBMI && !heldState.view(1100).held);

    // Stepping off the scale (clearWeight) keeps the held BMI visible.
    heldState.clearWeight();
    auto held = heldState.view(2000);
    assert(held.held && held.hasBMI && held.hasHeight && held.hasWeight && held.weightStable);
    assert(std::fabs(held.weightKg - 70.0f) < 0.001f);
    assert(std::fabs(held.heightCm - 175.0f) < 0.001f);

    // Walking out of the camera frame still preserves held BMI until holdMs expires.
    heldState.camera(2500, PersonStatus::None);
    held = heldState.view(12000);
    assert(held.held && held.hasBMI);
    assert(!heldState.view(13101).hasBMI && !heldState.view(13101).held);

    // A new person stepping onto the scale clears the previous hold immediately.
    heldState.camera(20000, PersonStatus::Measured, 175, 2.5f);
    heldState.weight(20100, 70, true);
    heldState.clearWeight();
    assert(heldState.view(20200).held);
    heldState.weight(20300, 58.5f, false);
    const auto nextPerson = heldState.view(20300);
    assert(!nextPerson.held && !nextPerson.hasBMI && nextPerson.hasWeight);
    assert(std::fabs(nextPerson.weightKg - 58.5f) < 0.001f);

    // Multiple people and explicit clearHold() cancel any held result immediately.
    heldState.weight(20500, 58.5f, true);
    heldState.clearWeight();
    assert(heldState.view(20600).held);
    heldState.camera(20700, PersonStatus::Multiple);
    assert(heldState.view(20700).status == PersonStatus::Multiple && !heldState.view(20700).held);
    heldState.camera(20800, PersonStatus::None);
    assert(!heldState.view(20800).held && !heldState.view(20800).hasBMI);

    heldState.camera(30000, PersonStatus::Measured, 170, 2.5f);
    heldState.weight(30100, 65, true);
    heldState.clearWeight();
    assert(heldState.view(30200).held);
    heldState.clearHold();
    assert(!heldState.view(30200).held && !heldState.view(30200).hasBMI);

    // Rollover safety for ResultHold.
    heldState.camera(0xfffff000U, PersonStatus::Measured, 180, 3);
    heldState.weight(0xfffff100U, 81, true);
    heldState.clearWeight();
    assert(heldState.view(0x00000100U).held && heldState.view(0x00000100U).hasBMI);
    assert(!heldState.view(0x00004000U).held && !heldState.view(0x00004000U).hasBMI);

    // Dynamic hold duration updates via setHoldMs().
    heldState.setHoldMs(15000);
    assert(heldState.holdMs() == 15000);
    heldState.camera(1000, PersonStatus::Measured, 175, 2.5f);
    heldState.weight(1100, 70, true);
    heldState.clearWeight();
    assert(heldState.view(14500).held && heldState.view(14500).hasBMI);
    assert(!heldState.view(16101).held && !heldState.view(16101).hasBMI);

    heldState.camera(20000, PersonStatus::Measured, 175, 2.5f);
    heldState.weight(20100, 70, true);
    heldState.clearWeight();
    assert(heldState.view(20200).held);
    heldState.setHoldMs(0);
    assert(heldState.holdMs() == 0);
    assert(!heldState.view(20201).held && !heldState.view(20201).hasBMI);

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
