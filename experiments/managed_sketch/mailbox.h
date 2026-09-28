#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace firmingo_managed {

// One producer and one consumer on different cores. Both indexes are monotonic;
// release/acquire orders byte writes before publication and reads before reuse.
class SpscByteRing {
 public:
  enum : std::uint32_t { capacity = 256 };

  std::size_t size() const {
    // Consumer only: its head cannot advance between these reads.
    const auto head = head_.load(std::memory_order_relaxed);
    const auto tail = tail_.load(std::memory_order_acquire);
    return tail - head;
  }
  std::size_t free_space() const {
    // Producer only: its tail cannot advance between these reads.
    const auto tail = tail_.load(std::memory_order_relaxed);
    const auto head = head_.load(std::memory_order_acquire);
    return capacity - (tail - head);
  }

  std::size_t push(const std::uint8_t* data, std::size_t count) {
    if (!data && count) return 0;
    std::uint32_t tail = tail_.load(std::memory_order_relaxed);
    const std::uint32_t head = head_.load(std::memory_order_acquire);
    std::size_t written = 0;
    while (written < count && tail - head < capacity) {
      bytes_[tail % capacity] = data[written++];
      ++tail;
    }
    tail_.store(tail, std::memory_order_release);
    return written;
  }

  std::size_t pop(std::uint8_t* data, std::size_t count) {
    if (!data && count) return 0;
    std::uint32_t head = head_.load(std::memory_order_relaxed);
    const std::uint32_t tail = tail_.load(std::memory_order_acquire);
    std::size_t read = 0;
    while (read < count && head != tail) {
      data[read++] = bytes_[head % capacity];
      ++head;
    }
    head_.store(head, std::memory_order_release);
    return read;
  }

 private:
  std::uint8_t bytes_[capacity] = {};
  std::atomic<std::uint32_t> head_{0}, tail_{0};
};

}  // namespace firmingo_managed
