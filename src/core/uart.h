#pragma once

#include "stream.h"

namespace firmingo {

// Narrow, nonblocking boundary implemented by each board port. A write accepts
// only bytes that can enter hardware immediately. take_rx_overrun() clears one
// latched hardware event; one event proves at least one lost byte.
class UartDriver {
 public:
  virtual ~UartDriver() = default;
  virtual bool configure(const SerialConfiguration& requested,
                         std::uint32_t& actual_baud) = 0;
  virtual bool active() const = 0;
  virtual bool tx_idle() const = 0;
  // Keep rx_reserve unused before accepting more locally originated TX. This
  // covers bytes already in the hardware transmitter/in flight when a peer
  // echoes or responds while network output is backpressured.
  virtual std::size_t rx_capacity() const = 0;
  virtual std::size_t rx_reserve() const = 0;
  virtual std::size_t rx_pending() = 0;
  virtual std::size_t read(std::uint8_t* data, std::size_t capacity) = 0;
  virtual std::size_t write(const std::uint8_t* data, std::size_t size) = 0;
  virtual bool take_rx_overrun() = 0;
};

class UartBackend : public StreamBackend {
 public:
  static constexpr std::uint32_t minimum_baud = 300;
  static constexpr std::uint32_t maximum_baud = 2000000;
  explicit UartBackend(UartDriver& driver) : driver_(driver) {}

  BackendKind kind() const override { return BackendKind::uart; }
  BackendResult prepare_open() override;
  bool configuration(SerialConfiguration& value) const override;
  BackendResult configure(const SerialConfiguration& value) override;
  bool connected() const override { return driver_.active(); }
  void discard() override;
  bool diagnostics(BackendDiagnostics& value) const override;

 protected:
  std::size_t read(std::uint8_t* data, std::size_t capacity) override;
  std::size_t write(const std::uint8_t* data, std::size_t size) override;

 private:
  static bool valid(const SerialConfiguration& value);
  static void add(std::uint64_t& value, std::size_t count);
  void observe() const;
  UartDriver& driver_;
  SerialConfiguration configuration_{};
  mutable std::size_t rx_peak_ = 0;
  mutable std::uint32_t overrun_events_ = 0;
  mutable std::uint64_t lost_bytes_minimum_ = 0;
  std::uint32_t tx_throttle_events_ = 0;
  std::uint64_t discarded_rx_ = 0;
};

}  // namespace firmingo
