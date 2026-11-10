// Runs the production display driver against a simulated ESP32 I2C bus.
#include <algorithm>
#include <cassert>
#include <cstring>
#include "../lib/Display/display.cpp"

MeasurementView measurementView() { return {}; }

static void resetTest() {
    delete lcd;
    lcd = nullptr;
    previous[0] = previous[1] = String();
    Wire = FakeWire{};
    testNow = lastLcdAttempt = 0;
    lcdStatus = "disabled";
}

int main() {
    static_assert(SDA == 11 && SCL == 12 && D5 == 8 && D6 == 9, "YOLO UNO GPIO mapping");
    // Non-default backpack address; reset a previously running wrong-pin bus.
    resetTest();
    Wire.started = true; Wire.sda = 38; Wire.scl = 39;
    Wire.devices[{11, 12}] = {0x20};
    initDisplay();
    assert(std::strcmp(displayStatus(), "ready") == 0);
    assert(lcd && lcd->address == 0x20);
    assert(Wire.sda == 11 && Wire.scl == 12); // LCD library called begin() again.
    assert(lcd->lines[0] == "No people       ");
    assert(lcd->lines[1] == "detected        ");

    // Working sketch's alternate GPIO pair, PCF8574A address 0x3e.
    resetTest();
    Wire.devices[{6, 7}] = {0x3e};
    initDisplay();
    assert(lcd && lcd->address == 0x3e && Wire.sda == 6 && Wire.scl == 7);

    // Never probe I2C on camera UART GPIO8/9; retry after a late LCD power-up.
    resetTest();
    Wire.devices[{8, 9}] = {0x27};
    initDisplay();
    assert(!lcd && std::strcmp(displayStatus(), "not-found") == 0);
    assert(std::find(Wire.scanned.begin(), Wire.scanned.end(), std::make_pair(8, 9)) == Wire.scanned.end());
    Wire.devices[{11, 12}] = {0x3f};
    testNow += 6000;
    updateDisplay();
    assert(lcd && lcd->address == 0x3f);

    // A random I2C sensor must not be treated as the LCD.
    resetTest();
    Wire.devices[{11, 12}] = {0x48};
    initDisplay();
    assert(!lcd);

    resetTest();
    Wire.devices[{11, 12}] = {0x20, 0x27};
    initDisplay();
    assert(!lcd && std::strcmp(displayStatus(), "ambiguous-address") == 0);
    resetTest();
    return 0;
}
