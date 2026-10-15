#include "stateful_upload.h"

#include <cstring>

extern "C" __attribute__((used)) const FirmingoStatefulUploadContract firmingo_stateful_upload_contract = {
    0x32505546, 1, 56, 2, 4096, 1024, 30000, "nano-managed-v2", "physical-d2-gnd"};
static_assert(sizeof(FirmingoStatefulUploadContract) == 56, "upload facts layout changed");

namespace firmingo_managed {
namespace stateful {
using firmingo::UploadError;
using firmingo::UploadState;
using firmingo::UploadStatus;

Upload::Upload(Contract contract, Runtime& runtime, Replacement& replacement, Check armed)
    : contract_(contract), runtime_(runtime), replacement_(replacement), armed_(armed),
      target_{firmingo_stateful_upload_contract.target_id, firmingo_stateful_upload_contract.format,
              firmingo_stateful_upload_contract.maximum_capsule_bytes,
              firmingo_stateful_upload_contract.maximum_chunk_bytes,
              firmingo_stateful_upload_contract.authorization} {}

bool Upload::blocks_console() const {
  return last_.id && (last_.state == UploadState::accepted || last_.state == UploadState::verified ||
                      last_.state == UploadState::committing);
}
UploadError Upload::check(std::uint32_t owner, std::uint32_t id, UploadState expected) const {
  if (!id || id != last_.id) return UploadError::wrong_target;
  if (!owner || owner != owner_) return UploadError::wrong_owner;
  if (last_.state != expected) return UploadError::invalid_state;
  return UploadError::none;
}
void Upload::clear_stage() { std::memset(stage_, 0, sizeof(stage_)); }
void Upload::fail(UploadError error) {
  clear_stage();
  commit_pending_ = false;
  installed_generation_ = 0;
  last_.state = UploadState::failed;
  last_.error = error;
}
UploadError Upload::begin(std::uint32_t owner, std::uint32_t size,
                          const std::uint8_t digest[32], UploadStatus& out) {
  if (!armed_ || !armed_()) return UploadError::unauthorized;
  if (blocks_console() || replacement_.active()) return UploadError::busy;
  if (size > sizeof(stage_) || size > contract_.maximum_capsule_bytes) return UploadError::insufficient_storage;
  if (!owner || !digest || size < 260 || contract_.maximum_capsule_bytes != target_.max_size)
    return UploadError::invalid_argument;
  if (!next_id_) return UploadError::io_error;
  clear_stage();
  last_ = {};
  last_.id = next_id_++;
  last_.size = size;
  last_.state = UploadState::accepted;
  std::memcpy(last_.digest, digest, 32);
  owner_ = owner;
  staged_at_ = now_;
  installed_generation_ = 0;
  out = last_;
  return UploadError::none;
}
UploadError Upload::chunk(std::uint32_t owner, std::uint32_t id, std::uint32_t offset,
                          const std::uint8_t* data, std::size_t size, UploadStatus& out) {
  const auto error = check(owner, id, UploadState::accepted);
  if (error != UploadError::none) return error;
  if (!data || !size || size > target_.max_chunk || size > last_.size - last_.received)
    return UploadError::invalid_argument;
  if (offset != last_.received) return UploadError::wrong_offset;
  std::memcpy(stage_ + offset, data, size);
  last_.received += static_cast<std::uint32_t>(size);
  out = last_;
  return UploadError::none;
}
UploadError Upload::finish(std::uint32_t owner, std::uint32_t id, UploadStatus& out) {
  const auto error = check(owner, id, UploadState::accepted);
  if (error != UploadError::none) return error;
  if (last_.received != last_.size) return UploadError::incomplete;
  std::uint8_t digest[32];
  sha256(stage_, last_.size, digest);
  if (std::memcmp(digest, last_.digest, 32)) { fail(UploadError::digest_mismatch); return last_.error; }
  if (validate_image(stage_, last_.size, contract_).error != Error::ok) {
    fail(UploadError::invalid_image); return last_.error;
  }
  last_.state = UploadState::verified;
  out = last_;
  return UploadError::none;
}
UploadError Upload::commit(std::uint32_t owner, std::uint32_t id, UploadStatus& out) {
  const auto error = check(owner, id, UploadState::verified);
  if (error != UploadError::none) return error;
  if (!armed_ || !armed_()) return UploadError::unauthorized;
  last_.state = UploadState::committing;
  commit_pending_ = true;
  out = last_;
  return UploadError::none;
}
UploadError Upload::abort(std::uint32_t owner, std::uint32_t id, UploadStatus& out) {
  if (!id || id != last_.id) return UploadError::wrong_target;
  if (!owner || owner != owner_) return UploadError::wrong_owner;
  if (last_.state != UploadState::accepted && last_.state != UploadState::verified)
    return UploadError::invalid_state;
  clear_stage(); last_.state = UploadState::aborted; out = last_;
  return UploadError::none;
}
UploadError Upload::status(std::uint32_t id, UploadStatus& out) const {
  if (!id || id != last_.id) return UploadError::wrong_target;
  out = last_; return UploadError::none;
}
void Upload::disconnect(std::uint32_t owner) {
  if (!last_.id || owner != owner_) return;
  if (last_.state == UploadState::accepted || last_.state == UploadState::verified) {
    clear_stage(); last_.state = UploadState::aborted; last_.error = UploadError::disconnected;
  }
}
void Upload::poll(std::uint32_t now) {
  now_ = now;
  if (!last_.id) return;
  if (last_.state == UploadState::installed && installed_generation_ &&
      runtime_.active_generation() == installed_generation_ && runtime_.loop_returned()) {
    last_.state = UploadState::boot_confirmed;
    return;
  }
  // Absolute stage lease, including commit queued before the next port poll.
  // Existing FMGO io_error denotes an expired stage; no new global wire error.
  if (blocks_console() && !replacement_.active() &&
      now - staged_at_ >= firmingo_stateful_upload_contract.staging_timeout_ms) {
    fail(UploadError::io_error); return;
  }
  if (last_.state != UploadState::committing) return;
  const bool armed = armed_ && armed_();
  if (commit_pending_) {
    commit_pending_ = false;
    if (!armed) { fail(UploadError::unauthorized); return; }
    if (!replacement_.request(stage_, last_.size, true, now)) { fail(UploadError::io_error); return; }
    clear_stage(); // Replacement owns its independent snapshot now.
  }
  const auto result = replacement_.poll(now, armed);
  last_.slot_touched = result.slot_touched;
  switch (result.status) {
    case ReplacementStatus::complete:
      installed_generation_ = runtime_.active_generation();
      last_.state = UploadState::installed; break;
    case ReplacementStatus::denied: fail(UploadError::unauthorized); break;
    case ReplacementStatus::timeout: fail(UploadError::park_timeout); break;
    case ReplacementStatus::failed: fail(UploadError::install_failed); break;
    case ReplacementStatus::idle: fail(UploadError::io_error); break;
    case ReplacementStatus::waiting: break;
  }
}
}  // namespace stateful
}  // namespace firmingo_managed
