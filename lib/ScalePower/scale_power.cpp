#include <Arduino.h>
#include <string.h>
#include <strings.h>
#include "config.h"
#include "scale_power.h"
#include "scale_power_state.h"

static ScalePowerState powerState(SCALE_POWER_IDLE_OFF_MS, SCALE_POWER_MIN_TOGGLE_MS);

static void writeOutput(bool on) {
#if SCALE_POWER_ENABLED
    const bool level = on ? SCALE_POWER_ACTIVE_HIGH : !SCALE_POWER_ACTIVE_HIGH;
    digitalWrite(SCALE_POWER_PIN, level ? HIGH : LOW);
    Serial.printf("[POWER] Scale supply %s (GPIO%d=%s)\n", on ? "ON" : "OFF",
                  SCALE_POWER_PIN, level ? "HIGH" : "LOW");
#else
    (void)on;
#endif
}

void initScalePower() {
#if SCALE_POWER_ENABLED
    powerState.begin(millis());
    pinMode(SCALE_POWER_PIN, OUTPUT);
    writeOutput(powerState.isOn());
    Serial.printf("[POWER] Auto off after %lu s idle. Serial: power on|off|auto|status\n",
                  (unsigned long)(SCALE_POWER_IDLE_OFF_MS / 1000));
#endif
}

void updateScalePower(bool activity) {
#if SCALE_POWER_ENABLED
    if (powerState.update(millis(), activity)) writeOutput(powerState.isOn());
#else
    (void)activity;
#endif
}

bool handleScalePowerCommand(const char *line) {
#if SCALE_POWER_ENABLED
    if (strncasecmp(line, "power", 5) != 0) return false;
    const char *arg = line + 5;
    while (*arg == ' ') ++arg;
    const uint32_t now = millis();
    if (strcasecmp(arg, "on") == 0) {
        powerState.setMode(now, ScalePowerMode::ForcedOn);
    } else if (strcasecmp(arg, "off") == 0) {
        powerState.setMode(now, ScalePowerMode::ForcedOff);
    } else if (strcasecmp(arg, "auto") == 0) {
        powerState.setMode(now, ScalePowerMode::Auto);
    } else if (strcasecmp(arg, "status") != 0) {
        Serial.println("[POWER] Use: power on | power off | power auto | power status");
        return true;
    }
    if (powerState.update(now, false)) writeOutput(powerState.isOn());
    Serial.printf("[POWER] %s\n", scalePowerStatus());
    return true;
#else
    (void)line;
    return false;
#endif
}

const char *scalePowerStatus() {
#if SCALE_POWER_ENABLED
    switch (powerState.mode()) {
        case ScalePowerMode::ForcedOn: return "on (manual)";
        case ScalePowerMode::ForcedOff: return "off (manual)";
        default: return powerState.isOn() ? "on (auto)" : "off (auto)";
    }
#else
    return "disabled";
#endif
}
