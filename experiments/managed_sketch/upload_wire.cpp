#include "upload_wire.h"

#include <cstring>

namespace firmingo_managed {

UploadReceiver::UploadReceiver(CapsuleStage& stage, std::uint32_t owner,
                               std::uint32_t api_address, const char* device_id)
    : stage_(stage), owner_(owner), api_address_(api_address),
      device_id_(device_id) {}

UploadStatus UploadReceiver::reject(UploadStatus reason) {
  stage_.abort(owner_);
  status_ = reason;
  return status_;
}

bool UploadReceiver::parse_header(bool authorized) {
  if (!authorized || !owner_ || !device_id_) {
    reject(UploadStatus::denied);
    return false;
  }
  if (std::memcmp(header_, "FMSU", 4) || header_[4] != 1 ||
      header_[5] || header_[6] || header_[7] ||
      std::strlen(device_id_) != 16 ||
      std::memcmp(header_ + 44, device_id_, 16)) {
    reject(UploadStatus::invalid);
    return false;
  }
  expected_bytes_ = std::uint32_t(header_[8]) |
                    (std::uint32_t(header_[9]) << 8) |
                    (std::uint32_t(header_[10]) << 16) |
                    (std::uint32_t(header_[11]) << 24);
  if (stage_.begin(owner_, true, expected_bytes_, header_ + 12,
                   api_address_) != StageResult::ok) {
    reject(UploadStatus::invalid);
    return false;
  }
  return true;
}

UploadStatus UploadReceiver::feed(const std::uint8_t* bytes, std::size_t size,
                                  bool authorized) {
  if (status_ != UploadStatus::receiving) {
    if (size && status_ == UploadStatus::ready) return reject(UploadStatus::invalid);
    return status_;
  }
  if (size && !bytes) return reject(UploadStatus::invalid);
  std::size_t position = 0;
  while (position < size) {
    if (header_bytes_ < kUploadHeaderBytes) {
      const std::size_t take = (size - position < kUploadHeaderBytes - header_bytes_)
                                   ? size - position : kUploadHeaderBytes - header_bytes_;
      std::memcpy(header_ + header_bytes_, bytes + position, take);
      header_bytes_ += take;
      position += take;
      if (header_bytes_ == kUploadHeaderBytes && !parse_header(authorized))
        return status_;
    } else if (capsule_bytes_ < expected_bytes_) {
      const std::size_t take = (size - position < expected_bytes_ - capsule_bytes_)
                                   ? size - position : expected_bytes_ - capsule_bytes_;
      if (stage_.append(owner_, capsule_bytes_, bytes + position, take)
          != StageResult::ok) return reject(UploadStatus::invalid);
      capsule_bytes_ += take;
      position += take;
      if (capsule_bytes_ == expected_bytes_ &&
          stage_.finish(owner_) != StageResult::ok)
        return reject(UploadStatus::invalid);
    } else {
      if (!authorized) return reject(UploadStatus::denied);
      static constexpr char commit[] = "GO!!";
      if (bytes[position++] != std::uint8_t(commit[commit_bytes_++]))
        return reject(UploadStatus::invalid);
      if (commit_bytes_ == 4) status_ = UploadStatus::ready;
      if (status_ == UploadStatus::ready && position < size)
        return reject(UploadStatus::invalid);
    }
  }
  return status_;
}

void UploadReceiver::abort() {
  stage_.abort(owner_);
  status_ = UploadStatus::invalid;
}

}  // namespace firmingo_managed
