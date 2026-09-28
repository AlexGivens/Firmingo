#pragma once

#include <atomic>
#include <cstdint>

namespace firmingo_managed {

// Cooperative boundary between an executing sketch loop and future flash work.
// Only core 0 requests/releases; only core 1 calls park_if_requested(). A
// stalled sketch cannot acknowledge, so callers must impose a deadline and
// refuse flash work when parked() stays false.
class Core1Pause {
 public:
  bool request();
  void release();
  bool parked() const;
  void park_if_requested();

 private:
  std::atomic<std::uint32_t> requested_{0};
  std::atomic<std::uint32_t> parked_{0};
};

}  // namespace firmingo_managed
