#include "scale_presence.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

int main() {
    MeasurementState weight;
    weight.weight(1, 65, true);
    assert(weight.weightView(1).hasWeight && weight.weightView(1).weightStable);
    assert(!weight.weightView(1).hasHeight && !weight.weightView(1).hasBMI);
    assert(!weight.view(1).hasWeight); // Camera-qualified view is unchanged.
    weight.clearWeight(2);
    assert(!weight.weightView(2).hasWeight);

    ScalePresence scale;
    scale.update(1, 5, true);
    assert(!scale.snapshot(1).present());
    scale.update(2, 65, false);
    assert(scale.snapshot(2).sessionId == 1 && scale.snapshot(2).present());
    scale.update(3, 65, true);
    assert(scale.snapshot(3).sessionId == 1 && scale.snapshot(3).measurement.weightStable);
    scale.update(4, 65, true, true); // Unload and reload between two app polls.
    scale.update(5, 80, true);
    assert(scale.snapshot(5).sessionId == 2);
    assert(std::fabs(scale.snapshot(5).measurement.weightKg - 80) < .01f);
    assert(!scale.snapshot(30006).present());
    scale.update(30007, 70, true);
    assert(scale.snapshot(30007).sessionId == 3);
    scale.update(30008, std::numeric_limits<float>::quiet_NaN(), true);
    assert(!scale.snapshot(30008).present());
    scale.update(0xfffffff0u, 60, true);
    assert(scale.snapshot(20).present());
    assert(!scale.snapshot(30000).present());
    std::puts("Scale presence/weight-only/session tests PASS");
}
