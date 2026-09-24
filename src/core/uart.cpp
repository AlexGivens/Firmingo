#include "uart.h"
#include <algorithm>

namespace firmingo {

constexpr std::uint32_t UartBackend::minimum_baud;
constexpr std::uint32_t UartBackend::maximum_baud;

bool UartBackend::valid(const SerialConfiguration& value) {
  return value.baud >= minimum_baud && value.baud <= maximum_baud &&
         value.data_bits >= 5 && value.data_bits <= 8 &&
         (value.stop_bits == 1 || value.stop_bits == 2) &&
         (value.parity == SerialParity::none ||
          value.parity == SerialParity::even ||
          value.parity == SerialParity::odd);
}

void UartBackend::add(std::uint64_t& value, std::size_t count) {
  value = count > UINT64_MAX - value ? UINT64_MAX : value + count;
}

void UartBackend::observe() const {
  const auto pending = driver_.rx_pending();
  if (pending > rx_peak_) rx_peak_ = pending;
  if (driver_.take_rx_overrun()) {
    if (overrun_events_ != UINT32_MAX) ++overrun_events_;
    if (lost_bytes_minimum_ != UINT64_MAX) ++lost_bytes_minimum_;
  }
}

bool UartBackend::configuration(SerialConfiguration& value) const {
  if (!driver_.active() || !configuration_.baud) return false;
  value = configuration_;
  return true;
}

BackendResult UartBackend::configure(const SerialConfiguration& value) {
  if (!valid(value)) return BackendResult::invalid_argument;
  if (driver_.active() && !driver_.tx_idle()) return BackendResult::busy;
  if (driver_.active()) discard();
  std::uint32_t actual = 0;
  if (!driver_.configure(value, actual) || !driver_.active() || !actual)
    return BackendResult::io_error;
  if (!driver_.rx_capacity() || driver_.rx_reserve() >= driver_.rx_capacity())
    return BackendResult::io_error;
  configuration_ = value;
  configuration_.actual_baud = actual;
  observe();
  return BackendResult::ok;
}

BackendResult UartBackend::prepare_open() {
  if (!driver_.active()) return BackendResult::io_error;
  return driver_.tx_idle() ? BackendResult::ok : BackendResult::busy;
}

std::size_t UartBackend::read(std::uint8_t* data, std::size_t capacity) {
  observe();
  const auto count = driver_.read(data, capacity);
  observe();
  return count;
}

std::size_t UartBackend::write(const std::uint8_t* data, std::size_t size) {
  observe();
  const auto pending = driver_.rx_pending();
  const auto capacity = driver_.rx_capacity();
  const auto reserve = driver_.rx_reserve();
  if (!capacity || reserve >= capacity || pending >= capacity - reserve) {
    if (tx_throttle_events_ != UINT32_MAX) ++tx_throttle_events_;
    return 0;
  }
  return driver_.write(data, std::min(size,capacity-reserve-pending));
}

void UartBackend::discard() {
  std::uint8_t bytes[Stream::capacity];
  observe();
  const auto count = driver_.read(bytes, sizeof(bytes));
  add(discarded_rx_, count);
  observe();
}

bool UartBackend::diagnostics(BackendDiagnostics& value) const {
  observe();
  value.rx_pending = driver_.rx_pending();
  value.rx_peak = std::max(rx_peak_, value.rx_pending);
  value.rx_discarded = discarded_rx_;
  value.rx_overrun_events = overrun_events_;
  value.rx_lost_bytes_minimum = lost_bytes_minimum_;
  value.tx_throttle_events = tx_throttle_events_;
  return true;
}

}  // namespace firmingo
