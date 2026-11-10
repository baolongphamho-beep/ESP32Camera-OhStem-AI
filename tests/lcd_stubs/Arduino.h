#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include "pins_arduino.h"

inline uint32_t testNow = 0;
inline uint32_t millis() { return testNow; }
inline void delay(uint32_t ms) { testNow += ms; }
class String {
public:
    std::string value;
    String() = default;
    String(const char* text) : value(text) {}
    String(const std::string& text) : value(text) {}
    String(float number, unsigned decimals) {
        char buffer[48]; std::snprintf(buffer, sizeof(buffer), "%.*f", int(decimals), double(number)); value = buffer;
    }
    unsigned length() const { return value.size(); }
    String substring(unsigned first, unsigned last) const { return value.substr(first, last - first); }
    String& operator+=(char ch) { value += ch; return *this; }
    bool operator!=(const String& other) const { return value != other.value; }
};
inline String operator+(const String& first, const String& second) { return first.value + second.value; }
struct TestSerial {
    template<class... Args> void printf(const char*, Args...) {}
    void println(const char*) {}
};
inline TestSerial Serial;
