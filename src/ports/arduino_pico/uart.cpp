#include "uart.h"
#include <algorithm>

namespace firmingo {

std::uint16_t ArduinoPicoUart::format(const SerialConfiguration& requested) {
  std::uint16_t value = requested.stop_bits == 1 ? SERIAL_STOP_BIT_1 : SERIAL_STOP_BIT_2;
  value |= requested.data_bits == 5 ? SERIAL_DATA_5 :
           requested.data_bits == 6 ? SERIAL_DATA_6 :
           requested.data_bits == 7 ? SERIAL_DATA_7 : SERIAL_DATA_8;
  value |= requested.parity == SerialParity::even ? SERIAL_PARITY_EVEN :
           requested.parity == SerialParity::odd ? SERIAL_PARITY_ODD : SERIAL_PARITY_NONE;
  return value;
}

bool ArduinoPicoUart::configure(const SerialConfiguration& requested,
                                std::uint32_t& actual_baud) {
  if (uart_) uart_.end();
  if (!uart_.setFIFOSize(receive_capacity)) return false;
  uart_.begin(requested.baud,format(requested));
  const int actual = uart_.getActualBaud();
  actual_baud = actual > 0 ? std::uint32_t(actual) : 0;
  return bool(uart_) && actual_baud != 0;
}

bool ArduinoPicoUart::active() const { return bool(uart_); }
bool ArduinoPicoUart::tx_idle() const {
  return active() && hardware_ &&
         !(uart_get_hw(hardware_)->fr & UART_UARTFR_BUSY_BITS);
}

std::size_t ArduinoPicoUart::rx_pending() {
  return std::size_t(std::max(0,uart_.available()));
}

std::size_t ArduinoPicoUart::read(std::uint8_t* data, std::size_t capacity) {
  const auto count = std::min(capacity,rx_pending());
  std::size_t read = 0;
  while (read < count) {
    const int value = uart_.read();
    if (value < 0) break;
    data[read++] = std::uint8_t(value);
  }
  return read;
}

std::size_t ArduinoPicoUart::write(const std::uint8_t* data, std::size_t size) {
  std::size_t written = 0;
  while (written < size && uart_.availableForWrite() > 0) {
    if (uart_.write(data[written]) != 1) break;
    ++written;
  }
  return written;
}

bool ArduinoPicoUart::take_rx_overrun() { return uart_.overflow(); }

}  // namespace firmingo
