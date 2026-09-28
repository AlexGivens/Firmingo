#pragma once

#include "stage.h"

#include <cstddef>
#include <cstdint>

namespace firmingo_managed {

constexpr std::uint32_t kFlashXipAddress = 0x10000000;
constexpr std::size_t kProofSectorBytes = 4096;
constexpr std::size_t kProofPageBytes = 256;

enum class InstallResult {
  ok, no_verified_capsule, invalid_image, flash_begin_failed,
  erase_failed, program_failed, readback_failed,
};

struct InstallReport {
  InstallResult result;
  // If true, the old sketch must never be resumed. The sector may be partial.
  bool slot_touched;
};

// The caller must park core 1 and keep it parked until this returns and the
// active sketch generation is cleared or replaced. No network callback calls
// this function. The board port owns core-0 IRQ masking in begin/end.
class ProofFlashPort {
 public:
  virtual ~ProofFlashPort() = default;
  virtual bool begin() = 0;
  virtual void end() = 0;
  virtual bool erase(std::uint32_t flash_offset, std::size_t size) = 0;
  virtual bool program(std::uint32_t flash_offset, const std::uint8_t* ram_page,
                       std::size_t size) = 0;
  virtual const std::uint8_t* slot_data() const = 0;
};

InstallReport install_verified_capsule(const CapsuleStage& stage,
                                       std::uint32_t owner,
                                       std::uint32_t api_address,
                                       ProofFlashPort& flash);

}  // namespace firmingo_managed
