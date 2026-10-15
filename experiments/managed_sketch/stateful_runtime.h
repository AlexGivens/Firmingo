#pragma once

#include "core1_pause.h"
#include "install.h"
#include "stateful_image.h"

#include <atomic>

namespace firmingo_managed {
namespace stateful {

// Entry calls run on core 1. stopped() runs on core 0 only before any entry has
// been published or while core 1 is parked; ports discard old console/backend IO.
class ExecutionPort {
 public:
  virtual ~ExecutionPort() = default;
  virtual void stopped() = 0;
  virtual void invoke(std::uint32_t thumb_address) = 0;
};

class Replacement;

// One core-0 lifecycle owner, one core-1 tick caller. Boot runs before publishing
// any sketch; subsequent stop/start is private to the parked replacement driver.
// Owned arena and execution port must outlive this object. Sketch code remains
// owner-authorized native code, not a memory/peripheral sandbox.
class Runtime {
 public:
  Runtime(Arena arena, Contract contract, ExecutionPort& port)
      : contract_(contract), generation_(arena), port_(port) {}
  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;
  Result boot_from_slot(const std::uint8_t* slot, std::size_t mapped_bytes);
  // Called only at the board's cooperative core-1 boundary, after its park check.
  bool tick();
  std::uint32_t active_generation() const {
    return published_.load(std::memory_order_acquire);
  }
  bool loop_returned() const;

 private:
  friend class Replacement;
  Result start_from_slot(const std::uint8_t* slot, std::size_t mapped_bytes);
  Result start_exact(const std::uint8_t* image, std::size_t bytes);
  void stop_quiescent();
  bool can_start() const;
  const Contract contract_;
  Generation generation_;
  ExecutionPort& port_;
  ImageInfo image_{};  // Written only before release-publication, with core 1 quiescent.
  std::atomic<std::uint32_t> published_{0}, returned_{0};
  std::uint32_t observed_ = 0;  // Core 1 only.
  bool boot_attempted_ = false;  // Core 0 only.
};

enum class ReplacementStatus { idle, waiting, complete, denied, timeout, failed };
struct ReplacementReport {
  ReplacementStatus status;
  InstallResult install;
  Error image_error;
  bool slot_touched;
};

// Firmware-side driver only; no FMGO target/SDK endpoint is introduced here.
// request() captures an immutable, exact capsule from caller-owned input. poll()
// must run on core 0 outside USB/lwIP callbacks/locks. No timeout or denial may
// stop the old generation or write flash; after stopping, no failure resumes it.
class Replacement {
 public:
  static constexpr std::uint32_t pause_timeout_ms = 1000;
  Replacement(Runtime& runtime, Core1Pause& pause, ProofFlashPort& flash)
      : runtime_(runtime), pause_(pause), flash_(flash) {}
  bool request(const std::uint8_t* image, std::size_t bytes,
               bool physically_armed, std::uint32_t now);
  ReplacementReport poll(std::uint32_t now, bool physically_armed);
  bool active() const { return bytes_ != 0; }

 private:
  InstallReport install();
  void release();
  Runtime& runtime_;
  Core1Pause& pause_;
  ProofFlashPort& flash_;
  std::uint8_t snapshot_[kMaximumCapsuleBytes]{};
  std::size_t bytes_ = 0;
  std::uint32_t started_at_ = 0;
};

}  // namespace stateful
}  // namespace firmingo_managed
