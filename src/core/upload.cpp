#include "upload.h"
namespace firmingo {
const char* upload_error_name(UploadError value) {
  switch (value) {
    case UploadError::none: return "none";
    case UploadError::unsupported: return "unsupported";
    case UploadError::invalid_argument: return "invalid_argument";
    case UploadError::wrong_target: return "wrong_target";
    case UploadError::invalid_state: return "invalid_state";
    case UploadError::busy: return "busy";
    case UploadError::unauthorized: return "unauthorized";
    case UploadError::wrong_owner: return "wrong_owner";
    case UploadError::wrong_offset: return "wrong_offset";
    case UploadError::incomplete: return "incomplete";
    case UploadError::digest_mismatch: return "digest_mismatch";
    case UploadError::invalid_image: return "invalid_image";
    case UploadError::insufficient_storage: return "insufficient_storage";
    case UploadError::io_error: return "io_error";
    case UploadError::disconnected: return "disconnected";
    case UploadError::park_timeout: return "park_timeout";
    case UploadError::install_failed: return "install_failed";
  }
  return "io_error";
}
const char* upload_state_name(UploadState value) {
  switch (value) {
    case UploadState::accepted: return "accepted";
    case UploadState::verified: return "verified";
    case UploadState::committing: return "committing";
    case UploadState::installed: return "installed";
    case UploadState::boot_confirmed: return "boot_confirmed";
    case UploadState::aborted: return "aborted";
    case UploadState::failed: return "failed";
  }
  return "failed";
}
}  // namespace firmingo
