#pragma once
#include <cstdint>

namespace firmingo {
struct TransportDiagnostics {
  uint32_t ncm_worker_runs = 0, ncm_rx_frames = 0, ncm_rx_deferred = 0;
  uint32_t ncm_rx_batch_peak = 0, ncm_mutex_contentions = 0;
  uint32_t ncm_budget_exhaustions = 0, ncm_wake_requests = 0;
  uint32_t tcp_accepts = 0, tcp_rx_callbacks = 0, tcp_sent_callbacks = 0, tcp_errors = 0;
  uint64_t tcp_rx_bytes = 0, tcp_sent_bytes = 0;
};

// Application-context samples only. Minima persist for this object's lifetime;
// they are not exhaustive heap/stack high-water measurements.
class MemorySamples {
 public:
  void observe(uint32_t heap_free, uint32_t stack_free) {
    heap_free_ = heap_free; stack_free_ = stack_free;
    if (!count_ || heap_free < heap_min_) heap_min_ = heap_free;
    if (!count_ || stack_free < stack_min_) stack_min_ = stack_free;
    if (count_ != UINT32_MAX) ++count_;
  }
  uint32_t count() const { return count_; }
  uint32_t heap_free() const { return heap_free_; }
  uint32_t heap_min() const { return heap_min_; }
  uint32_t stack_free() const { return stack_free_; }
  uint32_t stack_min() const { return stack_min_; }
  void observe_transport(const TransportDiagnostics& value) {
    transport_ = value; has_transport_ = true;
  }
  bool transport(TransportDiagnostics& value) const {
    if (!has_transport_) return false;
    value = transport_; return true;
  }
 private:
  uint32_t count_ = 0, heap_free_ = 0, heap_min_ = 0, stack_free_ = 0, stack_min_ = 0;
  TransportDiagnostics transport_{};
  bool has_transport_ = false;
};
}
