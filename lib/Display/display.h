#pragma once
void initDisplay();
void updateDisplay();
const char* displayStatus();
// Servo-sweep mode only: the two 16-char rows (from lib/EdgeSweep/edge_io.h).
void setDisplayLines(const char* line0, const char* line1);
// LCD backlight; off in STANDBY to save power (Plan 2 lcdSetBacklight).
void setDisplayBacklight(bool on);
