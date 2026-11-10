#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

// YOLO UNO pinout supplied with the board schematic. Arduino aliases map to GPIO.
#define USB_VID 0x303a
#define USB_PID 0x1001
static const uint8_t TX = 43;
static const uint8_t RX = 44;
static const uint8_t SDA = 11;
static const uint8_t SCL = 12;
static const uint8_t SS = 21;
static const uint8_t MOSI = 38;
static const uint8_t MISO = 47;
static const uint8_t SCK = 48;
static const uint8_t A0 = 1;
static const uint8_t A1 = 2;
static const uint8_t A2 = 3;
static const uint8_t A3 = 4;
static const uint8_t A4 = 11;
static const uint8_t A5 = 12;
static const uint8_t A6 = 5;
static const uint8_t D3 = 6;
static const uint8_t D4 = 7;
static const uint8_t D5 = 8;
static const uint8_t D6 = 9;
static const uint8_t D7 = 10;
static const uint8_t D8 = 17;
static const uint8_t D9 = 18;
static const uint8_t D10 = 21;
static const uint8_t D11 = 38;
static const uint8_t D12 = 47;
static const uint8_t D13 = 48;
#define LED_BUILTIN 48
#define BUILTIN_LED LED_BUILTIN
#define PIN_RGB_LED 45
#define RGB_BUILTIN PIN_RGB_LED
#define RGB_BRIGHTNESS 64

#endif
