#pragma once

#include "mailbox.h"
#include <core/application.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace firmingo_managed {

// Portable bridge between one sketch core and the existing application stream.
// Sketch methods and sketch_boundary() run only on core 1; poll() and stop()
// run only on core 0. No method is ISR-safe. A new owner must reach a sketch
// loop boundary before it can receive sketch bytes, so an old loop's output
// cannot be delivered to the new owner.
class SketchConsole {
 public:
  enum : std::size_t { capacity = SpscByteRing::capacity };

  struct Counters {
    std::uint32_t input_discarded = 0;
    std::uint32_t output_discarded = 0;
    std::uint32_t output_rejected = 0;
    std::uint32_t output_peak = 0;
    std::uint32_t loop_boundaries = 0;
    std::uint32_t epoch = 0;
    std::uint32_t acknowledged_epoch = 0;
    bool input_enabled = false;
    bool output_enabled = false;
  };

  void sketch_boundary() {
    add(loop_boundaries_, 1);
    const auto epoch = epoch_.load(std::memory_order_acquire);
    if (ack_.load(std::memory_order_relaxed) == epoch) return;
    std::uint8_t discarded[capacity];
    add(input_discarded_, to_sketch_.pop(discarded, sizeof(discarded)));
    ack_.store(epoch, std::memory_order_release);
  }

  std::uint32_t sketch_write(const std::uint8_t* data, std::uint32_t size) {
    if (!output_enabled_.load(std::memory_order_acquire)) {
      add(output_rejected_, size);
      return 0;
    }
    const auto written = from_sketch_.push(data, size);
    add(output_rejected_, size - written);
    const auto occupied = capacity - from_sketch_.free_space();
    update_peak(output_peak_, occupied);
    return static_cast<std::uint32_t>(written);
  }

  std::uint32_t sketch_write_available() const {
    return output_enabled_.load(std::memory_order_acquire)
        ? static_cast<std::uint32_t>(from_sketch_.free_space()) : 0;
  }

  std::int32_t sketch_read() {
    const auto epoch = epoch_.load(std::memory_order_acquire);
    if (!input_enabled_.load(std::memory_order_acquire) ||
        ack_.load(std::memory_order_acquire) != epoch) return -1;
    std::uint8_t value = 0;
    if (!to_sketch_.pop(&value, 1)) return -1;
    if (epoch_.load(std::memory_order_acquire) != epoch ||
        !input_enabled_.load(std::memory_order_acquire)) {
      add(input_discarded_, 1);
      return -1;
    }
    return value;
  }

  std::uint32_t sketch_available() const {
    const auto epoch = epoch_.load(std::memory_order_acquire);
    if (!input_enabled_.load(std::memory_order_acquire) ||
        ack_.load(std::memory_order_acquire) != epoch) return 0;
    const auto count = to_sketch_.size();
    return epoch_.load(std::memory_order_acquire) == epoch &&
                   input_enabled_.load(std::memory_order_acquire)
        ? static_cast<std::uint32_t>(count) : 0;
  }

  // scratch must hold the entire 256-byte ring. Returns false only for an
  // invalid scratch buffer or an impossible short write to the backend.
  bool poll(firmingo::ApplicationEndpoint& backend, std::uint8_t* scratch,
            std::size_t scratch_size) {
    if (!scratch || scratch_size < capacity) return false;
    const auto owner = backend.owner();
    const auto generation = backend.generation();
    const bool active = backend.active();
    if (owner != observed_owner_ || generation != observed_generation_ ||
        active != observed_active_) {
      input_enabled_.store(false, std::memory_order_release);
      output_enabled_.store(false, std::memory_order_release);
      observed_owner_ = owner;
      observed_generation_ = generation;
      observed_active_ = active;
      bump_epoch();
    }
    const auto epoch = epoch_.load(std::memory_order_acquire);
    if (!active || ack_.load(std::memory_order_acquire) != epoch) {
      discard_output(scratch);
      return true;
    }
    if (!output_enabled_.load(std::memory_order_relaxed)) {
      discard_output(scratch);  // Any accepted bytes from the old loop.
      input_enabled_.store(true, std::memory_order_release);
      output_enabled_.store(true, std::memory_order_release);
    }
    const auto outgoing = from_sketch_.pop(
        scratch, std::min(scratch_size, backend.available_for_write()));
    if (outgoing && backend.write_to_host(scratch, outgoing) != outgoing)
      return false;
    const auto incoming = backend.read_from_host(
        scratch, std::min(scratch_size, to_sketch_.free_space()));
    return !incoming || to_sketch_.push(scratch, incoming) == incoming;
  }

  void stop(std::uint8_t* scratch, std::size_t scratch_size) {
    input_enabled_.store(false, std::memory_order_release);
    output_enabled_.store(false, std::memory_order_release);
    bump_epoch();
    if (scratch && scratch_size >= capacity) discard_output(scratch);
  }

  Counters counters() const {
    return {input_discarded_.load(std::memory_order_relaxed),
            output_discarded_.load(std::memory_order_relaxed),
            output_rejected_.load(std::memory_order_relaxed),
            output_peak_.load(std::memory_order_relaxed),
            loop_boundaries_.load(std::memory_order_relaxed),
            epoch_.load(std::memory_order_acquire),
            ack_.load(std::memory_order_acquire),
            input_enabled_.load(std::memory_order_acquire),
            output_enabled_.load(std::memory_order_acquire)};
  }

 private:
  // Each counter has exactly one writer core; readers only load atomically.
  // An RP2040 atomic RMW takes a shared hardware spinlock, which is avoidable
  // for these single-writer values (including the high-frequency loop count).
  static void add(std::atomic<std::uint32_t>& counter, std::size_t amount) {
    const auto before = counter.load(std::memory_order_relaxed);
    counter.store(amount > UINT32_MAX - before ? UINT32_MAX :
                      before + static_cast<std::uint32_t>(amount),
                  std::memory_order_release);
  }
  static void update_peak(std::atomic<std::uint32_t>& peak,
                          std::size_t occupied) {
    const auto before = peak.load(std::memory_order_relaxed);
    if (occupied > before)
      peak.store(static_cast<std::uint32_t>(occupied),
                 std::memory_order_release);
  }
  void bump_epoch() {
    // Core 0 is the only writer; the core-1 reader uses an acquire load.
    epoch_.store(epoch_.load(std::memory_order_relaxed) + 1,
                 std::memory_order_release);
  }
  void discard_output(std::uint8_t* scratch) {
    add(output_discarded_, from_sketch_.pop(scratch, capacity));
  }

  SpscByteRing to_sketch_, from_sketch_;
  std::atomic<std::uint32_t> epoch_{0}, ack_{0};
  std::atomic<bool> input_enabled_{false}, output_enabled_{false};
  std::atomic<std::uint32_t> input_discarded_{0}, output_discarded_{0},
      output_rejected_{0}, output_peak_{0}, loop_boundaries_{0};
  std::uint32_t observed_owner_ = 0, observed_generation_ = 0;
  bool observed_active_ = false;
};

}  // namespace firmingo_managed
