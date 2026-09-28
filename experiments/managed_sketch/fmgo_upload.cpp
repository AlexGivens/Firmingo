#include "fmgo_upload.h"
#include <cstring>

namespace firmingo_managed {
using firmingo::UploadError;
using firmingo::UploadState;
using firmingo::UploadStatus;
const firmingo::UploadTarget& FmgoUpload::target() const {
  static const firmingo::UploadTarget value{1,"nano-managed-v1",kProofStageBytes,1024,"physical-d2-gnd"};
  return value;
}
bool FmgoUpload::blocks_console() const {
  return last_.id && (last_.state == UploadState::accepted ||
      last_.state == UploadState::verified || last_.state == UploadState::committing);
}
UploadError FmgoUpload::check(uint32_t owner, uint32_t id, UploadState expected) const {
  if (!id || id != last_.id) return UploadError::wrong_target;
  if (!owner || owner != owner_) return UploadError::wrong_owner;
  if (last_.state != expected) return UploadError::invalid_state;
  return UploadError::none;
}
UploadError FmgoUpload::begin(uint32_t owner, uint32_t size, const uint8_t digest[32], UploadStatus& out) {
  if (!armed_ || !armed_()) return UploadError::unauthorized;
  if (blocks_console() || stage_.active() || switching_.active()) return UploadError::busy;
  if (size > kProofStageBytes) return UploadError::insufficient_storage;
  if (!owner || !digest || size <= kCodeOffset || !api_) return UploadError::invalid_argument;
  if (!next_id_) return UploadError::io_error; // Never reuse an ID in this boot.
  if (stage_.begin(owner,true,size,digest,api_) != StageResult::ok) return UploadError::io_error;
  last_ = UploadStatus{};
  last_.id = next_id_++;
  last_.size = size;
  last_.state = UploadState::accepted;
  std::memcpy(last_.digest,digest,32);
  owner_ = owner;
  out = last_;
  return UploadError::none;
}
UploadError FmgoUpload::chunk(uint32_t owner, uint32_t id, uint32_t offset,
                            const uint8_t* data, std::size_t size, UploadStatus& out) {
  const auto error = check(owner,id,UploadState::accepted);
  if (error != UploadError::none) return error;
  if (!size || size > target().max_chunk || !data) return UploadError::invalid_argument;
  if (offset != last_.received) return UploadError::wrong_offset;
  if (size > last_.size - last_.received) return UploadError::invalid_argument;
  if (stage_.append(owner,offset,data,size) != StageResult::ok) return UploadError::io_error;
  last_.received += uint32_t(size); out = last_;
  return UploadError::none;
}
void FmgoUpload::fail(UploadError error) {
  if (stage_.active()) stage_.abort(owner_);
  commit_pending_ = false;
  last_.state = UploadState::failed;
  last_.error = error;
}
UploadError FmgoUpload::finish(uint32_t owner, uint32_t id, UploadStatus& out) {
  const auto error = check(owner,id,UploadState::accepted);
  if (error != UploadError::none) return error;
  const auto result = stage_.finish(owner);
  if (result == StageResult::incomplete) return UploadError::incomplete;
  if (result != StageResult::ok) {
    const auto failed = result == StageResult::digest_mismatch ? UploadError::digest_mismatch : UploadError::invalid_image;
    fail(failed); return failed;
  }
  last_.state = UploadState::verified; out = last_;
  return UploadError::none;
}
UploadError FmgoUpload::commit(uint32_t owner, uint32_t id, UploadStatus& out) {
  const auto error = check(owner,id,UploadState::verified);
  if (error != UploadError::none) return error;
  if (!armed_ || !armed_()) return UploadError::unauthorized;
  last_.state = UploadState::committing; commit_pending_ = true; out = last_;
  return UploadError::none; // No pause/erase/program under the Session's lock.
}
UploadError FmgoUpload::abort(uint32_t owner, uint32_t id, UploadStatus& out) {
  if (!id || id != last_.id) return UploadError::wrong_target;
  if (!owner || owner != owner_) return UploadError::wrong_owner;
  if (last_.state != UploadState::accepted && last_.state != UploadState::verified) return UploadError::invalid_state;
  if (stage_.abort(owner) != StageResult::ok) return UploadError::io_error;
  last_.state = UploadState::aborted; out = last_;
  return UploadError::none;
}
UploadError FmgoUpload::status(uint32_t id, UploadStatus& out) const {
  if (!id || id != last_.id) return UploadError::wrong_target;
  out = last_; return UploadError::none;
}
void FmgoUpload::disconnect(uint32_t owner) {
  if (owner != owner_ || !last_.id) return;
  if (last_.state == UploadState::accepted || last_.state == UploadState::verified) {
    stage_.abort(owner);
    last_.state = UploadState::aborted;
    last_.error = UploadError::disconnected;
  }
  // Committing is irrevocable from the socket's perspective. A reconnect
  // queries this retained record; it never automatically retries commit.
}
void FmgoUpload::poll(uint32_t now) {
  if (!last_.id) return;
  if (last_.state == UploadState::installed && generation_returned_ && generation_returned_()) {
    last_.state = UploadState::boot_confirmed;
    return;
  }
  if (last_.state != UploadState::committing) return;
  const bool armed = armed_ && armed_();
  if (commit_pending_) {
    commit_pending_ = false;
    if (!armed) { fail(UploadError::unauthorized); return; }
    if (!switching_.begin(owner_,api_,now)) { fail(UploadError::io_error); return; }
  }
  const auto report = switching_.poll(now,armed);
  last_.slot_touched = report.slot_touched;
  switch (report.status) {
    case SwitchStatus::complete: last_.state = UploadState::installed; break;
    case SwitchStatus::denied: fail(UploadError::unauthorized); break;
    case SwitchStatus::timeout: fail(UploadError::park_timeout); break;
    case SwitchStatus::install_failed: fail(UploadError::install_failed); break;
    case SwitchStatus::waiting: break;
    case SwitchStatus::idle: fail(UploadError::io_error); break;
  }
}
}  // namespace firmingo_managed
