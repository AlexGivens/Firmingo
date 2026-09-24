#pragma once

#include <cstddef>
#include <cstdint>

namespace firmingo {

struct BackendDiagnostics {
  std::size_t rx_pending = 0;
  std::size_t tx_pending = 0;
  std::size_t rx_peak = 0;
  std::size_t tx_peak = 0;
  std::uint64_t rx_discarded = 0;
  std::uint64_t tx_discarded = 0;
  std::uint32_t rx_overrun_events = 0;
  std::uint64_t rx_lost_bytes_minimum = 0;
  std::uint32_t tx_throttle_events = 0;
};

enum class BackendKind { application, uart };
enum class SerialParity { none, even, odd };
struct SerialConfiguration {
  constexpr SerialConfiguration(std::uint32_t requested = 0,
                                std::uint32_t actual = 0,
                                std::uint8_t bits = 0,
                                std::uint8_t stops = 0,
                                SerialParity serial_parity = SerialParity::none)
      : baud(requested), actual_baud(actual), data_bits(bits),
        stop_bits(stops), parity(serial_parity) {}
  std::uint32_t baud = 0;
  std::uint32_t actual_baud = 0;
  std::uint8_t data_bits = 0;
  std::uint8_t stop_bits = 0;
  SerialParity parity = SerialParity::none;
};
enum class BackendResult { ok, unsupported, invalid_argument, busy, io_error };

// Adapters must return immediately: 0 means backpressure, not EOF. Return at
// most capacity/size. Report EOF or hardware failure through connected().
// Call only from one application context, never concurrently or from an ISR.
class ByteIO {
 public:
  virtual ~ByteIO() = default;
  virtual bool connected() const = 0;
  virtual std::size_t read(std::uint8_t* data, std::size_t capacity) = 0;
  virtual std::size_t write(const std::uint8_t* data, std::size_t size) = 0;
};

// A backend used by a session owns bounded queues that can be disposed on
// disconnect. discard must not reset unrelated application state.
class StreamBackend : public ByteIO {
 public:
  virtual BackendKind kind() const { return BackendKind::application; }
  virtual BackendResult prepare_open() {
    return connected() ? BackendResult::ok : BackendResult::io_error;
  }
  virtual bool configuration(SerialConfiguration&) const { return false; }
  virtual BackendResult configure(const SerialConfiguration&) {
    return BackendResult::unsupported;
  }
  virtual void discard() = 0;
  virtual void opened(std::uint32_t) {}
  virtual void closed() {}
  virtual bool diagnostics(BackendDiagnostics&) const { return false; }
};

enum class StreamResult {
  ok, busy, invalid_owner, disconnected, timeout, io_error,
  unsupported, invalid_argument
};

// One connection owns the stream. The port handles encoding a busy/error
// response, socket closure and backend-specific cleanup after a result.
class Stream {
 public:
  static constexpr std::size_t capacity = 256;  // per direction
  explicit Stream(std::uint32_t idle_timeout_ms = 30000);
  StreamResult open(std::uint32_t owner, std::uint32_t now_ms);
  StreamResult close(std::uint32_t owner);
  StreamResult poll(std::uint32_t owner, ByteIO& peer, ByteIO& backend,
                    std::uint32_t now_ms);
  std::uint32_t owner() const { return owner_; }
  std::size_t pending_to_backend() const { return to_backend_.size; }
  std::size_t pending_to_peer() const { return to_peer_.size; }

  std::size_t peak_to_backend() const { return to_backend_.peak; }
  std::size_t peak_to_peer() const { return to_peer_.peak; }

 private:
  struct Pending {
    std::uint8_t bytes[capacity]{};
    std::size_t offset = 0;
    std::size_t size = 0, peak = 0;
  };
  static bool pump(ByteIO& source, ByteIO& destination, Pending& pending,
                   bool& progress);
  void clear();
  Pending to_backend_;
  Pending to_peer_;
  std::uint32_t owner_ = 0;
  std::uint32_t last_progress_ = 0;
  const std::uint32_t idle_timeout_ms_;
};
}  // namespace firmingo
