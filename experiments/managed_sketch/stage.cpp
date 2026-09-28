#include "stage.h"

#include <cstring>

namespace firmingo_managed {

void CapsuleStage::clear() {
  std::memset(bytes_, 0, sizeof(bytes_));
  std::memset(expected_digest_, 0, sizeof(expected_digest_));
  owner_ = api_address_ = 0;
  expected_bytes_ = received_ = 0;
  verified_ = false;
}

StageResult CapsuleStage::begin(std::uint32_t owner, bool authorized,
                                std::size_t total_bytes,
                                const std::uint8_t digest[32],
                                std::uint32_t api_address) {
  if (!authorized) return StageResult::unauthorized;
  if (owner_) return StageResult::busy;
  if (!owner || !digest || total_bytes <= kCodeOffset ||
      total_bytes > kProofStageBytes || !api_address)
    return StageResult::invalid_argument;
  owner_ = owner;
  api_address_ = api_address;
  expected_bytes_ = total_bytes;
  received_ = 0;
  verified_ = false;
  std::memcpy(expected_digest_, digest, 32);
  return StageResult::ok;
}

StageResult CapsuleStage::append(std::uint32_t owner, std::size_t offset,
                                 const std::uint8_t* data, std::size_t size) {
  if (!owner_ || verified_) return StageResult::invalid_state;
  if (owner != owner_) return StageResult::wrong_owner;
  if (!data || !size) return StageResult::invalid_argument;
  if (offset != received_) return StageResult::wrong_offset;
  if (size > expected_bytes_ - received_) return StageResult::invalid_argument;
  std::memcpy(bytes_ + received_, data, size);
  received_ += size;
  return StageResult::ok;
}

StageResult CapsuleStage::finish(std::uint32_t owner) {
  if (!owner_ || verified_) return StageResult::invalid_state;
  if (owner != owner_) return StageResult::wrong_owner;
  if (received_ != expected_bytes_) return StageResult::incomplete;
  std::uint8_t digest[32];
  sha256(bytes_, expected_bytes_, digest);
  if (std::memcmp(digest, expected_digest_, 32)) {
    clear();
    return StageResult::digest_mismatch;
  }
  ImageInfo info{};
  if (validate_image(bytes_, expected_bytes_, api_address_, &info) != ImageError::ok ||
      expected_bytes_ != kCodeOffset + info.code_bytes) {
    clear();
    return StageResult::invalid_image;
  }
  verified_ = true;
  return StageResult::ok;
}

StageResult CapsuleStage::abort(std::uint32_t owner) {
  if (!owner_) return StageResult::invalid_state;
  if (owner != owner_) return StageResult::wrong_owner;
  clear();
  return StageResult::ok;
}

const std::uint8_t* CapsuleStage::verified(std::uint32_t owner,
                                            std::size_t* size) const {
  if (!verified_ || owner != owner_ || !size) return nullptr;
  *size = expected_bytes_;
  return bytes_;
}

}  // namespace firmingo_managed
