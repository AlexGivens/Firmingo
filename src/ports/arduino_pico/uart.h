#pragma once

#include "core/uart.h"
#include <SerialUART.h>
#include <hardware/uart.h>

namespace firmingo {

// Arduino-Pico SerialUART adapter. RX uses a fixed 256-byte interrupt queue.
// TX checks the hardware FIFO before each byte so the core's blocking bulk
// write implementation is never called without immediate capacity.
class ArduinoPicoUart : public UartDriver {
 public:
  ArduinoPicoUart(SerialUART& uart, uart_inst_t* hardware)
      : uart_(uart), hardware_(hardware) {}
  bool configure(const SerialConfiguration& requested,
                 std::uint32_t& actual_baud) override;
  bool active() const override;
  bool tx_idle() const override;
  std::size_t rx_capacity() const override { return receive_capacity; }
  std::size_t rx_reserve() const override { return receive_reserve; }
  std::size_t rx_pending() override;
  std::size_t read(std::uint8_t* data, std::size_t capacity) override;
  std::size_t write(const std::uint8_t* data, std::size_t size) override;
  bool take_rx_overrun() override;

 private:
  static constexpr std::size_t receive_capacity = Stream::capacity;
  static constexpr std::size_t receive_reserve = 64;
  static std::uint16_t format(const SerialConfiguration& requested);
  SerialUART& uart_;
  uart_inst_t* hardware_;
};

}  // namespace firmingo
