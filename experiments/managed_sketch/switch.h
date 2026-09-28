#pragma once

#include "core1_pause.h"
#include "install.h"

#include <cstdint>

namespace firmingo_managed {

enum class SwitchStatus { idle, waiting, complete, timeout, denied, install_failed };

struct SwitchReport {
  SwitchStatus status;
  InstallResult install;
  bool slot_touched;
};

class SketchRuntime {
 public:
  virtual ~SketchRuntime() = default;
  // Called only after core 1 acknowledges its SRAM park point.
  virtual void stop() = 0;
  virtual void start(const ImageInfo& image) = 0;
};

// Core-0 loop driver. Never call begin/poll under the lwIP lock or from a USB
// callback. A timeout refuses to touch flash and lets the old sketch continue.
class SketchSwitch {
 public:
  static constexpr std::uint32_t pause_timeout_ms = 1000;
  SketchSwitch(CapsuleStage& stage, Core1Pause& pause, ProofFlashPort& flash,
               SketchRuntime& runtime)
      : stage_(stage), pause_(pause), flash_(flash), runtime_(runtime) {}
  bool begin(std::uint32_t owner, std::uint32_t api_address, std::uint32_t now);
  SwitchReport poll(std::uint32_t now, bool physically_armed);
  bool active() const { return owner_ != 0; }

 private:
  CapsuleStage& stage_;
  Core1Pause& pause_;
  ProofFlashPort& flash_;
  SketchRuntime& runtime_;
  std::uint32_t owner_ = 0, api_address_ = 0, started_at_ = 0;
};

}  // namespace firmingo_managed
