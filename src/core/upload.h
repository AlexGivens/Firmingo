#pragma once
#include <cstddef>
#include <cstdint>

namespace firmingo {
enum class UploadError {
  none, unsupported, invalid_argument, wrong_target, invalid_state, busy,
  unauthorized, wrong_owner, wrong_offset, incomplete, digest_mismatch,
  invalid_image, insufficient_storage, io_error, disconnected, park_timeout,
  install_failed,
};
enum class UploadState { accepted, verified, committing, installed, boot_confirmed, aborted, failed };
const char* upload_error_name(UploadError value);
const char* upload_state_name(UploadState value);
struct UploadTarget {
  uint32_t id;
  const char* format;
  uint32_t max_size, max_chunk;
  const char* authorization;
};
struct UploadStatus {
  uint32_t id = 0, size = 0, received = 0;
  uint8_t digest[32]{};
  UploadState state = UploadState::aborted;
  UploadError error = UploadError::none;
  bool slot_touched = false;
};
// Optional device service shared by sessions. All methods are serialized by
// the port. These methods only stage/request work; they MUST NOT write flash.
// The port drives actual installation later, outside USB/lwIP callbacks/locks.
class UploadService {
 public:
  virtual ~UploadService() = default;
  virtual const UploadTarget& target() const = 0;
  virtual UploadError begin(uint32_t owner, uint32_t size, const uint8_t digest[32], UploadStatus&) = 0;
  virtual UploadError chunk(uint32_t owner, uint32_t id, uint32_t offset,
                            const uint8_t* data, std::size_t size, UploadStatus&) = 0;
  virtual UploadError finish(uint32_t owner, uint32_t id, UploadStatus&) = 0;
  virtual UploadError commit(uint32_t owner, uint32_t id, UploadStatus&) = 0;
  virtual UploadError abort(uint32_t owner, uint32_t id, UploadStatus&) = 0;
  virtual UploadError status(uint32_t id, UploadStatus&) const = 0;
  virtual void disconnect(uint32_t owner) = 0;
  virtual bool blocks_console() const = 0;
};
}  // namespace firmingo
