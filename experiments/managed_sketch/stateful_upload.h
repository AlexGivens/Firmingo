#pragma once

#include "core/upload.h"
#include "stateful_runtime.h"

// Readonly, independently versioned firmware upload facts exported from ELF.
struct FirmingoStatefulUploadContract {
  std::uint32_t magic;
  std::uint16_t version, bytes;
  std::uint32_t target_id, maximum_capsule_bytes, maximum_chunk_bytes, staging_timeout_ms;
  char format[16], authorization[16];
};
extern "C" const FirmingoStatefulUploadContract firmingo_stateful_upload_contract;

namespace firmingo_managed {
namespace stateful {

// Session callbacks only stage/verify/queue. poll() alone may request a pause or
// drive flash, outside USB/lwIP locks. One boot-lifetime status record survives
// socket disconnect; committing operations cannot be replayed or aborted by it.
class Upload final : public firmingo::UploadService {
 public:
  using Check = bool (*)();
  Upload(Contract contract, Runtime& runtime, Replacement& replacement, Check armed);
  const firmingo::UploadTarget& target() const override { return target_; }
  firmingo::UploadError begin(std::uint32_t owner, std::uint32_t size, const std::uint8_t digest[32], firmingo::UploadStatus&) override;
  firmingo::UploadError chunk(std::uint32_t owner, std::uint32_t id, std::uint32_t offset,
                            const std::uint8_t* data, std::size_t size, firmingo::UploadStatus&) override;
  firmingo::UploadError finish(std::uint32_t owner, std::uint32_t id, firmingo::UploadStatus&) override;
  firmingo::UploadError commit(std::uint32_t owner, std::uint32_t id, firmingo::UploadStatus&) override;
  firmingo::UploadError abort(std::uint32_t owner, std::uint32_t id, firmingo::UploadStatus&) override;
  firmingo::UploadError status(std::uint32_t id, firmingo::UploadStatus&) const override;
  void disconnect(std::uint32_t owner) override;
  bool blocks_console() const override;
  void poll(std::uint32_t now);

 private:
  firmingo::UploadError check(std::uint32_t owner, std::uint32_t id, firmingo::UploadState expected) const;
  void clear_stage();
  void fail(firmingo::UploadError error);
  const Contract contract_;
  Runtime& runtime_;
  Replacement& replacement_;
  const Check armed_;
  const firmingo::UploadTarget target_;
  std::uint8_t stage_[kMaximumCapsuleBytes]{};
  firmingo::UploadStatus last_{};
  std::uint32_t owner_ = 0, next_id_ = 1, now_ = 0, staged_at_ = 0, installed_generation_ = 0;
  bool commit_pending_ = false;
};

}  // namespace stateful
}  // namespace firmingo_managed
