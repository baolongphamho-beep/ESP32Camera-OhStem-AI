// LCD half of edge_io.h for env:yolo_uno_servo_sweep, on top of lib/Display (I2C 16x2).
#ifdef SERVO_SWEEP_MODE
#include "edge_io.h"

#include "display.h"

void lcdSetBacklight(bool on) { setDisplayBacklight(on); }

LcdLines lcdShowPhase(SystemPhase phase, EdgeNotice notice, const LcdData& data) {
    const LcdLines lines = lcdTextFor(phase, notice, data);
    setDisplayLines(lines.line[0], lines.line[1]);
    updateDisplay();  // Writes only the rows that changed.
    return lines;
}
#endif
