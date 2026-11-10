#pragma once
#include <map>
#include <set>
#include <utility>
#include <vector>
#include "Arduino.h"

struct FakeWire {
    int sda = -1, scl = -1;
    bool started = false;
    uint8_t address = 0;
    std::map<std::pair<int, int>, std::set<uint8_t>> devices;
    std::vector<std::pair<int, int>> scanned;
    bool end() { started = false; return true; }
    bool setPins(int data, int clock) {
        if (started) return false;
        sda = data; scl = clock; return true;
    }
    bool begin(int data = -1, int clock = -1, unsigned = 0) {
        // Match ESP32: begin on an active bus returns without changing pins.
        if (started) return true;
        sda = data < 0 ? (sda < 0 ? SDA : sda) : data;
        scl = clock < 0 ? (scl < 0 ? SCL : scl) : clock;
        started = true;
        scanned.emplace_back(sda, scl);
        return true;
    }
    void setTimeOut(unsigned) {}
    void beginTransmission(uint8_t addr) { address = addr; }
    int endTransmission() { return devices[{sda, scl}].count(address) ? 0 : 2; }
};
inline FakeWire Wire;
