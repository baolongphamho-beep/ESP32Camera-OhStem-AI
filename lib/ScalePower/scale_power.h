#pragma once
#include <stdint.h>

// YOLO UNO switches the scale supply through a relay or MOSFET module.
// Pin, active level and timings live in lib/Config/config.h.
void initScalePower();
void updateScalePower(bool activity);       // Call from loop().
bool handleScalePowerCommand(const char *line);  // "power on|off|auto|status"
const char *scalePowerStatus();
