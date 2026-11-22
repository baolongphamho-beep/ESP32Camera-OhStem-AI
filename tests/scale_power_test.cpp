// g++ -std=c++17 -I lib/ScalePower tests/scale_power_test.cpp -o .pio/scale_power_test.exe
#include <cassert>
#include <cstdint>
#include "scale_power_state.h"

int main() {
    ScalePowerState power(120000, 3000);
    power.begin(0);
    assert(power.isOn());                       // On at boot.
    assert(!power.update(60000, false));
    assert(power.isOn());                       // Still within idle window.
    assert(power.update(120000, false));
    assert(!power.isOn());                      // Off after 2 min idle.
    assert(!power.update(121000, true));      // Too soon after switching off.
    assert(power.update(123000, true));
    assert(power.isOn());                       // Person seen -> on again.
    assert(!power.update(123500, true));        // No repeat toggles.

    // Anti-chatter: off then immediate activity waits for min toggle time.
    ScalePowerState quick(1000, 3000);
    quick.begin(0);
    assert(quick.update(3000, false) && !quick.isOn());
    assert(!quick.update(4000, true) && !quick.isOn());
    assert(quick.update(6000, true) && quick.isOn());

    // Manual modes override activity and are not delayed.
    power.setMode(130000, ScalePowerMode::ForcedOff);
    assert(power.update(130001, true) && !power.isOn());
    power.setMode(130002, ScalePowerMode::ForcedOn);
    assert(power.update(130003, false) && power.isOn());
    power.setMode(130004, ScalePowerMode::Auto);
    assert(!power.update(200000, false) && power.isOn());  // Auto restarts idle timer.
    assert(power.update(250004, false) && !power.isOn());

    // millis() rollover.
    ScalePowerState wrap(1000, 0);
    wrap.begin(0xfffffc00U);
    assert(!wrap.update(0xffffff00U, false));
    assert(wrap.update(0x00000100U, false) && !wrap.isOn());
    return 0;
}
