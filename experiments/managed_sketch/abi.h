#pragma once

#include <stdint.h>

// Deliberately narrow experimental ABI for a separately linked Nano sketch.
// The resident publishes this table at the exact address embedded in the
// capsule. Function calls are made on the sketch core, never in a USB callback.
struct FirmingoSketchApiV1 {
  uint32_t magic;       // "FAPI" as a little-endian integer
  uint16_t abi_version;
  uint16_t table_bytes;
  void (*pin_mode)(uint32_t pin, uint32_t mode);
  void (*digital_write)(uint32_t pin, uint32_t value);
  uint32_t (*clock_ms)();
  void (*delay_ms)(uint32_t milliseconds);
  uint32_t (*console_write)(const uint8_t* data, uint32_t size);
  uint32_t (*console_write_available)();
  int32_t (*console_read)();
  uint32_t (*console_available)();
};

constexpr uint32_t kFirmingoApiMagic = 0x49504146;  // "FAPI"
