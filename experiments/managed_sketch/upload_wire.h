#pragma once

#include "stage.h"

#include <cstddef>
#include <cstdint>

namespace firmingo_managed {

// Private M7 proof wire format, separate from the FMGO public protocol:
// FMSU, version=1, three zero bytes, LE32 capsule size, SHA-256 of capsule,
// 16 lowercase ASCII hex device-ID bytes, then capsule, then "GO!!".
constexpr std::size_t kUploadHeaderBytes = 60;
enum class UploadStatus { receiving, ready, denied, invalid };

class UploadReceiver {
 public:
  UploadReceiver(CapsuleStage& stage, std::uint32_t owner,
                 std::uint32_t api_address, const char* device_id);
  UploadStatus feed(const std::uint8_t* bytes, std::size_t size, bool authorized);
  void abort();
  UploadStatus status() const { return status_; }

 private:
  UploadStatus reject(UploadStatus reason);
  bool parse_header(bool authorized);
  CapsuleStage& stage_;
  const std::uint32_t owner_, api_address_;
  const char* const device_id_;
  std::uint8_t header_[kUploadHeaderBytes]{};
  std::size_t header_bytes_ = 0, capsule_bytes_ = 0, expected_bytes_ = 0;
  std::size_t commit_bytes_ = 0;
  UploadStatus status_ = UploadStatus::receiving;
};

}  // namespace firmingo_managed
