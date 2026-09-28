#pragma once

// This intentionally implements only the APIs used by the two proof sketches.
// It is not the Arduino-Pico core and does not imply general library support.
#include "../abi.h"
#include <stddef.h>

#ifndef FIRMINGO_API_ADDRESS
#error "Build a sketch capsule for one exact resident API address"
#endif

static inline const FirmingoSketchApiV1& firmingoSketchApi() {
  return *reinterpret_cast<const FirmingoSketchApiV1*>(FIRMINGO_API_ADDRESS);
}

using byte = uint8_t;
constexpr uint32_t LED_BUILTIN = 6;  // Nano RP2040 Connect D13, pinned core 6.0.0.
constexpr uint32_t OUTPUT = 1;
constexpr uint32_t HIGH = 1;
constexpr uint32_t LOW = 0;

static inline void pinMode(uint32_t pin, uint32_t mode) {
  firmingoSketchApi().pin_mode(pin, mode);
}
static inline void digitalWrite(uint32_t pin, uint32_t value) {
  firmingoSketchApi().digital_write(pin, value);
}
static inline uint32_t millis() {
  return firmingoSketchApi().clock_ms();
}
static inline void delay(uint32_t milliseconds) {
  firmingoSketchApi().delay_ms(milliseconds);
}

class FirmingoSketchSerial {
 public:
  void begin(unsigned long baud) const {
    // Compatibility call: network console has no hardware baud rate.
    (void)baud;
  }
  explicit operator bool() const { return true; }  // Service, not peer, ready.
  int available() const { return int(firmingoSketchApi().console_available()); }
  int availableForWrite() const {
    return int(firmingoSketchApi().console_write_available());
  }
  int read() const { return int(firmingoSketchApi().console_read()); }
  size_t write(uint8_t value) const {
    return firmingoSketchApi().console_write(&value, 1);
  }
  size_t write(const uint8_t* data, size_t size) const {
    return firmingoSketchApi().console_write(data, uint32_t(size));
  }
  size_t print(const char* text) const {
    size_t length = 0;
    while (text[length]) ++length;
    return write(reinterpret_cast<const uint8_t*>(text), length);
  }
  size_t println(const char* text) const {
    const size_t count = print(text);
    const uint8_t ending[2] = {'\r', '\n'};
    return count + write(ending, 2);
  }
};

// A temporary object avoids mutable module globals. The resident owns queues.
#define Serial FirmingoSketchSerial()
