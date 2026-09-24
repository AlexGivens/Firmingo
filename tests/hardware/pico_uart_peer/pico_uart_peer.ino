// Firmingo hardware-test fixture for an independent 3.3 V UART peer.

#include <Arduino.h>

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

constexpr uint32_t kDefaultBaud = 115200;
constexpr uint32_t kMinimumBaud = 300;
constexpr uint32_t kMaximumBaud = 2000000;
constexpr size_t kCommandCapacity = 96;

enum class ReceiveMode : uint8_t { Echo, Sink, Expect };
enum class Parity : uint8_t { None, Even, Odd };

char command[kCommandCapacity];
size_t command_length = 0;
uint32_t requested_baud = kDefaultBaud;
uint8_t configured_data_bits = 8;
Parity configured_parity = Parity::None;
uint8_t configured_stop_bits = 1;
ReceiveMode receive_mode = ReceiveMode::Echo;
uint64_t uart_rx_bytes = 0;
uint64_t uart_tx_bytes = 0;
uint32_t uart_overrun_events = 0;
uint32_t mismatches = 0;
uint32_t first_mismatch = UINT32_MAX;
uint32_t expected_total = 0;
uint32_t expected_remaining = 0;
uint32_t expected_state = 0;
uint32_t send_total = 0;
uint32_t send_remaining = 0;
uint32_t send_state = 0;

uint8_t next_byte(uint32_t& state) {
  state = state * 1664525u + 1013904223u;
  return static_cast<uint8_t>(state >> 24);
}

uint8_t next_uart_byte(uint32_t& state) {
  const uint8_t mask = static_cast<uint8_t>((1u << configured_data_bits) - 1u);
  return next_byte(state) & mask;
}

void saturating_increment(uint32_t& value) {
  if (value != UINT32_MAX) {
    ++value;
  }
}

bool parse_u32(const char* text, uint32_t& value) {
  if (!text || !*text || *text == '-') {
    return false;
  }
  char* end = nullptr;
  errno = 0;
  const unsigned long parsed = strtoul(text, &end, 0);
  if (errno == ERANGE || !end || *end != '\0' || parsed > UINT32_MAX) {
    return false;
  }
  value = static_cast<uint32_t>(parsed);
  return true;
}

const char* mode_name() {
  switch (receive_mode) {
    case ReceiveMode::Echo:
      return "echo";
    case ReceiveMode::Sink:
      return "sink";
    case ReceiveMode::Expect:
      return "expect";
  }
  return "invalid";
}

const char* parity_name(Parity parity) {
  switch (parity) {
    case Parity::None:
      return "none";
    case Parity::Even:
      return "even";
    case Parity::Odd:
      return "odd";
  }
  return "invalid";
}

bool parse_parity(const char* text, Parity& parity) {
  if (!text) {
    return false;
  }
  if (!strcmp(text, "none")) {
    parity = Parity::None;
  } else if (!strcmp(text, "even")) {
    parity = Parity::Even;
  } else if (!strcmp(text, "odd")) {
    parity = Parity::Odd;
  } else {
    return false;
  }
  return true;
}

uint16_t serial_format(uint8_t data_bits, Parity parity, uint8_t stop_bits) {
  uint16_t value = stop_bits == 1 ? SERIAL_STOP_BIT_1 : SERIAL_STOP_BIT_2;
  value |= data_bits == 5 ? SERIAL_DATA_5 :
           data_bits == 6 ? SERIAL_DATA_6 :
           data_bits == 7 ? SERIAL_DATA_7 : SERIAL_DATA_8;
  value |= parity == Parity::Even ? SERIAL_PARITY_EVEN :
           parity == Parity::Odd ? SERIAL_PARITY_ODD : SERIAL_PARITY_NONE;
  return value;
}

void print_stats(const char* prefix) {
  Serial.printf(
      "%s baud=%lu actual_baud=%d mode=%s rx=%llu tx=%llu "
      "data_bits=%u parity=%s stop_bits=%u "
      "overruns=%lu mismatches=%lu first_mismatch=%lu "
      "expect_remaining=%lu send_remaining=%lu\n",
      prefix, static_cast<unsigned long>(requested_baud),
      Serial1.getActualBaud(), mode_name(),
      static_cast<unsigned long long>(uart_rx_bytes),
      static_cast<unsigned long long>(uart_tx_bytes),
      static_cast<unsigned>(configured_data_bits), parity_name(configured_parity),
      static_cast<unsigned>(configured_stop_bits),
      static_cast<unsigned long>(uart_overrun_events),
      static_cast<unsigned long>(mismatches),
      static_cast<unsigned long>(first_mismatch),
      static_cast<unsigned long>(expected_remaining),
      static_cast<unsigned long>(send_remaining));
}

void clear_results() {
  uart_rx_bytes = 0;
  uart_tx_bytes = 0;
  uart_overrun_events = 0;
  mismatches = 0;
  first_mismatch = UINT32_MAX;
  expected_total = 0;
  expected_remaining = 0;
  send_total = 0;
  send_remaining = 0;
  while (Serial1.available() > 0) {
    Serial1.read();
  }
  (void)Serial1.overflow();
}

void configure_uart(uint32_t baud, uint8_t data_bits, Parity parity,
                    uint8_t stop_bits) {
  Serial1.flush();
  Serial1.end();
  Serial1.setTX(0);
  Serial1.setRX(1);
  Serial1.setFIFOSize(256);
  Serial1.begin(baud, serial_format(data_bits, parity, stop_bits));
  requested_baud = baud;
  configured_data_bits = data_bits;
  configured_parity = parity;
  configured_stop_bits = stop_bits;
}

void print_configuration() {
  Serial.printf("OK baud=%lu actual_baud=%d data_bits=%u parity=%s stop_bits=%u\n",
                static_cast<unsigned long>(requested_baud),
                Serial1.getActualBaud(), static_cast<unsigned>(configured_data_bits),
                parity_name(configured_parity),
                static_cast<unsigned>(configured_stop_bits));
}

void handle_command(char* line) {
  char* save = nullptr;
  const char* operation = strtok_r(line, " ", &save);
  if (!operation) {
    return;
  }
  if (!strcmp(operation, "PING")) {
    print_stats("OK peer=pico-uart-peer-v2");
    return;
  }
  if (!strcmp(operation, "STATS")) {
    print_stats("OK");
    return;
  }
  if (!strcmp(operation, "CLEAR")) {
    clear_results();
    receive_mode = ReceiveMode::Echo;
    Serial.println("OK cleared");
    return;
  }
  if (!strcmp(operation, "BAUD")) {
    uint32_t baud = 0;
    const char* argument = strtok_r(nullptr, " ", &save);
    if (!parse_u32(argument, baud) || baud < kMinimumBaud || baud > kMaximumBaud ||
        strtok_r(nullptr, " ", &save)) {
      Serial.println("ERR invalid_baud");
      return;
    }
    if (expected_remaining || send_remaining) {
      Serial.println("ERR busy");
      return;
    }
    configure_uart(baud, configured_data_bits, configured_parity,
                   configured_stop_bits);
    print_configuration();
    return;
  }
  if (!strcmp(operation, "CONFIG")) {
    uint32_t baud = 0;
    uint32_t data_bits = 0;
    uint32_t stop_bits = 0;
    Parity parity = Parity::None;
    const char* baud_text = strtok_r(nullptr, " ", &save);
    const char* data_bits_text = strtok_r(nullptr, " ", &save);
    const char* parity_text = strtok_r(nullptr, " ", &save);
    const char* stop_bits_text = strtok_r(nullptr, " ", &save);
    if (!parse_u32(baud_text, baud) || baud < kMinimumBaud ||
        baud > kMaximumBaud || !parse_u32(data_bits_text, data_bits) ||
        data_bits < 5 || data_bits > 8 || !parse_parity(parity_text, parity) ||
        !parse_u32(stop_bits_text, stop_bits) ||
        (stop_bits != 1 && stop_bits != 2) || strtok_r(nullptr, " ", &save)) {
      Serial.println("ERR invalid_config");
      return;
    }
    if (expected_remaining || send_remaining) {
      Serial.println("ERR busy");
      return;
    }
    configure_uart(baud, static_cast<uint8_t>(data_bits), parity,
                   static_cast<uint8_t>(stop_bits));
    print_configuration();
    return;
  }
  if (!strcmp(operation, "MODE")) {
    const char* argument = strtok_r(nullptr, " ", &save);
    if (!argument || strtok_r(nullptr, " ", &save) || expected_remaining) {
      Serial.println("ERR invalid_mode");
      return;
    }
    if (!strcmp(argument, "ECHO")) {
      receive_mode = ReceiveMode::Echo;
    } else if (!strcmp(argument, "SINK")) {
      receive_mode = ReceiveMode::Sink;
    } else {
      Serial.println("ERR invalid_mode");
      return;
    }
    Serial.printf("OK mode=%s\n", mode_name());
    return;
  }
  if (!strcmp(operation, "EXPECT") || !strcmp(operation, "SEND")) {
    uint32_t count = 0;
    uint32_t seed = 0;
    const char* count_text = strtok_r(nullptr, " ", &save);
    const char* seed_text = strtok_r(nullptr, " ", &save);
    if (!parse_u32(count_text, count) || !parse_u32(seed_text, seed) || count == 0 ||
        strtok_r(nullptr, " ", &save) || expected_remaining || send_remaining) {
      Serial.println("ERR invalid_transfer");
      return;
    }
    if (!strcmp(operation, "EXPECT")) {
      expected_total = count;
      expected_remaining = count;
      expected_state = seed;
      mismatches = 0;
      first_mismatch = UINT32_MAX;
      receive_mode = ReceiveMode::Expect;
      Serial.printf("OK expect=%lu seed=%lu\n", static_cast<unsigned long>(count),
                    static_cast<unsigned long>(seed));
    } else {
      send_total = count;
      send_remaining = count;
      send_state = seed;
      Serial.printf("OK send=%lu seed=%lu\n", static_cast<unsigned long>(count),
                    static_cast<unsigned long>(seed));
    }
    return;
  }
  Serial.println("ERR unsupported");
}

void service_usb_control() {
  while (Serial.available() > 0) {
    const int value = Serial.read();
    if (value < 0) {
      break;
    }
    if (value == '\r') {
      continue;
    }
    if (value == '\n') {
      command[command_length] = '\0';
      handle_command(command);
      command_length = 0;
      continue;
    }
    if (command_length + 1 < kCommandCapacity) {
      command[command_length++] = static_cast<char>(value);
    } else {
      command_length = 0;
      Serial.println("ERR command_too_long");
    }
  }
}

void service_uart_receive() {
  if (Serial1.overflow()) {
    saturating_increment(uart_overrun_events);
  }
  while (Serial1.available() > 0) {
    if (receive_mode == ReceiveMode::Echo && Serial1.availableForWrite() <= 0) {
      break;
    }
    const int value = Serial1.read();
    if (value < 0) {
      break;
    }
    ++uart_rx_bytes;
    if (receive_mode == ReceiveMode::Echo) {
      Serial1.write(static_cast<uint8_t>(value));
      ++uart_tx_bytes;
    } else if (receive_mode == ReceiveMode::Expect && expected_remaining) {
      const uint32_t offset = expected_total - expected_remaining;
      if (static_cast<uint8_t>(value) != next_uart_byte(expected_state)) {
        saturating_increment(mismatches);
        if (first_mismatch == UINT32_MAX) {
          first_mismatch = offset;
        }
      }
      --expected_remaining;
      if (!expected_remaining) {
        receive_mode = ReceiveMode::Sink;
        print_stats("EVENT expect_done");
      }
    }
  }
}

void service_uart_send() {
  while (send_remaining && Serial1.availableForWrite() > 0) {
    Serial1.write(next_uart_byte(send_state));
    ++uart_tx_bytes;
    --send_remaining;
    if (!send_remaining) {
      print_stats("EVENT send_done");
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  configure_uart(kDefaultBaud, 8, Parity::None, 1);
}

void loop() {
  service_usb_control();
  service_uart_receive();
  service_uart_send();
}
