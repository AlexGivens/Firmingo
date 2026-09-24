#pragma once

#include "stream.h"
#include <algorithm>
#include <cstring>

namespace firmingo {

// A bounded, nonblocking stream endpoint for firmware application code.
// All calls run serially in application context; none are ISR-safe.
class ApplicationEndpoint : public StreamBackend {
 public:
  enum : std::size_t { capacity = 256 };  // per direction

  bool active() const { return active_; }
  std::uint32_t owner() const { return owner_; }
  std::uint32_t generation() const { return generation_; }
  std::size_t available() const { return rx_.size; }
  std::size_t available_for_write() const { return capacity - tx_.size; }
  std::size_t pending_to_host() const { return tx_.size; }
  std::uint64_t discarded_from_host() const { return discarded_rx_; }
  std::uint64_t discarded_to_host() const { return discarded_tx_; }

  std::size_t read_from_host(std::uint8_t* data, std::size_t size) {
    return active_ ? pop(rx_, data, size) : 0;
  }
  std::size_t write_to_host(const std::uint8_t* data, std::size_t size) {
    return active_ ? push(tx_, data, size) : 0;
  }

  bool connected() const override { return true; }
  void discard() override {
    add(discarded_rx_, rx_.size);
    add(discarded_tx_, tx_.size);
    rx_.head = rx_.size = tx_.head = tx_.size = 0;
  }
  void opened(std::uint32_t owner) override {
    active_ = true;
    owner_ = owner;
    if (generation_ != UINT32_MAX) ++generation_;
  }
  void closed() override { active_ = false; owner_ = 0; }
  bool diagnostics(BackendDiagnostics& value) const override {
    value.rx_pending = rx_.size;
    value.tx_pending = tx_.size;
    value.rx_peak = rx_.peak;
    value.tx_peak = tx_.peak;
    value.rx_discarded = discarded_rx_;
    value.tx_discarded = discarded_tx_;
    return true;
  }

 private:
  struct Ring {
    std::uint8_t bytes[capacity]{};
    std::size_t head = 0, size = 0, peak = 0;
  };
  static void add(std::uint64_t& value, std::size_t count) {
    value = count > UINT64_MAX - value ? UINT64_MAX : value + count;
  }
  static std::size_t push(Ring& ring, const std::uint8_t* data, std::size_t size) {
    const std::size_t count = std::min(size, capacity - ring.size);
    const std::size_t tail = (ring.head + ring.size) % capacity;
    const std::size_t first = std::min(count, capacity - tail);
    if (first) std::memcpy(ring.bytes + tail, data, first);
    if (count > first) std::memcpy(ring.bytes, data + first, count - first);
    ring.size += count;
    if (ring.size > ring.peak) ring.peak = ring.size;
    return count;
  }
  static std::size_t pop(Ring& ring, std::uint8_t* data, std::size_t size) {
    const std::size_t count = std::min(size, ring.size);
    const std::size_t first = std::min(count, capacity - ring.head);
    if (first) std::memcpy(data, ring.bytes + ring.head, first);
    if (count > first) std::memcpy(data + first, ring.bytes, count - first);
    ring.head = (ring.head + count) % capacity;
    ring.size -= count;
    if (!ring.size) ring.head = 0;
    return count;
  }

 protected:
  // Network-facing direction is only exposed to Channel and native test fakes.
  std::size_t read(std::uint8_t* data, std::size_t size) override {
    return active_ ? pop(tx_, data, size) : 0;
  }
  std::size_t write(const std::uint8_t* data, std::size_t size) override {
    return active_ ? push(rx_, data, size) : 0;
  }

 private:
  Ring rx_, tx_;
  std::uint32_t owner_ = 0, generation_ = 0;
  std::uint64_t discarded_rx_ = 0, discarded_tx_ = 0;
  bool active_ = false;
};
}  // namespace firmingo
