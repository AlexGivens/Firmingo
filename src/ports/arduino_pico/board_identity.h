#pragma once

// Arduino-Pico derives these symbols from the selected board's build.board.
// Keep the Firmingo identity stable and independent of USB/IP details.
#if defined(ARDUINO_NANO_RP2040_CONNECT)
#define FIRMINGO_BOARD_ID "nano_rp2040_connect"
#elif defined(ARDUINO_RASPBERRY_PI_PICO)
#define FIRMINGO_BOARD_ID "raspberry_pi_pico"
#else
#error "Firmingo has no declared identity for this Arduino-Pico board"
#endif
