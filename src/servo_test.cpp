// Servo SG92R test for YOLO UNO. Compiled only by env:yolo_uno_servo_test.
// Serial Monitor 115200, line ending "Newline". Commands:
//   0..180   -> move to that angle (degrees)
//   c        -> centre (90)
//   + / -    -> step 5 degrees up / down
//   sweep    -> sweep SERVO_TEST_MIN..SERVO_TEST_MAX and back
//   detach   -> stop sending pulses (servo goes limp, no buzzing)
#ifdef SERVO_TEST_MODE
#include <Arduino.h>
#include <ESP32Servo.h>

#ifndef SERVO_TEST_PIN
#define SERVO_TEST_PIN 2          // GPIO number (A1 on YOLO UNO), same as height servo
#endif
#ifndef SERVO_TEST_MIN_US
#define SERVO_TEST_MIN_US 500     // pulse width at 0 degrees
#endif
#ifndef SERVO_TEST_MAX_US
#define SERVO_TEST_MAX_US 2400    // pulse width at 180 degrees
#endif
#define SERVO_TEST_MIN 20         // safe sweep limits, same as height servo
#define SERVO_TEST_MAX 160

static Servo servo;
static int angle = 90;

static void attachIfNeeded() {
    if (!servo.attached()) {
        servo.setPeriodHertz(50);
        servo.attach(SERVO_TEST_PIN, SERVO_TEST_MIN_US, SERVO_TEST_MAX_US);
    }
}

static void moveTo(int target) {
    angle = constrain(target, 0, 180);
    attachIfNeeded();
    servo.write(angle);
    Serial.printf("[SERVO] angle=%d deg (pulse ~%d us)\n", angle,
                  (int)map(angle, 0, 180, SERVO_TEST_MIN_US, SERVO_TEST_MAX_US));
}

static void sweep() {
    Serial.println("[SERVO] sweep...");
    for (int a = SERVO_TEST_MIN; a <= SERVO_TEST_MAX; ++a) { attachIfNeeded(); servo.write(a); delay(15); }
    for (int a = SERVO_TEST_MAX; a >= SERVO_TEST_MIN; --a) { servo.write(a); delay(15); }
    moveTo(90);
}

static void handle(String cmd) {
    cmd.trim();
    cmd.toLowerCase();
    if (cmd.length() == 0) return;
    if (cmd == "c") moveTo(90);
    else if (cmd == "+") moveTo(angle + 5);
    else if (cmd == "-") moveTo(angle - 5);
    else if (cmd == "sweep") sweep();
    else if (cmd == "detach") { servo.detach(); Serial.println("[SERVO] detached"); }
    else if (isDigit(cmd[0])) moveTo(cmd.toInt());
    else Serial.println("[SERVO] Commands: 0..180 | c | + | - | sweep | detach");
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    ESP32PWM::allocateTimer(0);
    Serial.printf("[SERVO] SG92R test on GPIO%d, %d-%d us\n", SERVO_TEST_PIN,
                  SERVO_TEST_MIN_US, SERVO_TEST_MAX_US);
    moveTo(90);
    Serial.println("[SERVO] Commands: 0..180 | c | + | - | sweep | detach");
}

void loop() {
    static String line;
    while (Serial.available() > 0) {
        const char c = (char)Serial.read();
        if (c == '\n' || c == '\r') { handle(line); line = ""; }
        else if (line.length() < 16) line += c;
    }
    delay(5);
}
#endif
