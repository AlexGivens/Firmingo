#pragma once

#include "image.h"

#include <cstddef>
#include <cstdint>

namespace firmingo_managed {

// RAM-only M7 proof limit. A later general installer needs an explicit flash
// staging design; this accepts only the two small proof modules.
constexpr std::size_t kProofStageBytes = 4096;

enum class StageResult {
  ok, unauthorized, busy, invalid_argument, wrong_owner, invalid_state,
  wrong_offset, incomplete, digest_mismatch, invalid_image,
};

class CapsuleStage {
 public:
  StageResult begin(std::uint32_t owner, bool authorized, std::size_t total_bytes,
                    const std::uint8_t digest[32], std::uint32_t api_address);
  StageResult append(std::uint32_t owner, std::size_t offset,
                     const std::uint8_t* data, std::size_t size);
  StageResult finish(std::uint32_t owner);
  StageResult abort(std::uint32_t owner);
  const std::uint8_t* verified(std::uint32_t owner, std::size_t* size) const;
  std::size_t received() const { return received_; }
  bool active() const { return owner_ != 0; }

 private:
  void clear();
  std::uint8_t bytes_[kProofStageBytes]{};
  std::uint8_t expected_digest_[32]{};
  std::uint32_t owner_ = 0, api_address_ = 0;
  std::size_t expected_bytes_ = 0, received_ = 0;
  bool verified_ = false;
};

}  // namespace firmingo_managed
