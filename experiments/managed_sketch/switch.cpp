#include "switch.h"

namespace firmingo_managed {

bool SketchSwitch::begin(std::uint32_t owner, std::uint32_t api_address,
                         std::uint32_t now) {
  if (owner_ || !owner || !api_address) return false;
  std::size_t size = 0;
  if (!stage_.verified(owner, &size) || !size || !pause_.request()) return false;
  owner_ = owner;
  api_address_ = api_address;
  started_at_ = now;
  return true;
}

SwitchReport SketchSwitch::poll(std::uint32_t now, bool physically_armed) {
  if (!owner_) return {SwitchStatus::idle, InstallResult::no_verified_capsule, false};
  if (!physically_armed) {
    pause_.release();
    stage_.abort(owner_);
    owner_ = 0;
    return {SwitchStatus::denied, InstallResult::no_verified_capsule, false};
  }
  if (!pause_.parked()) {
    if (static_cast<std::uint32_t>(now - started_at_) < pause_timeout_ms)
      return {SwitchStatus::waiting, InstallResult::no_verified_capsule, false};
    pause_.release();
    stage_.abort(owner_);
    owner_ = 0;
    return {SwitchStatus::timeout, InstallResult::no_verified_capsule, false};
  }

  // Even an erase/program error can leave a partially changed active slot.
  // Stop the old generation before issuing the first flash operation, while
  // core 1 is still parked in SRAM. Never resume the old entry afterward.
  runtime_.stop();
  const auto installed = install_verified_capsule(stage_, owner_, api_address_, flash_);
  ImageInfo info{};
  InstallResult result = installed.result;
  if (result == InstallResult::ok &&
      validate_image(flash_.slot_data(), kProofSectorBytes, api_address_, &info)
          == ImageError::ok)
    runtime_.start(info);
  else if (result == InstallResult::ok)
    result = InstallResult::readback_failed;
  stage_.abort(owner_);
  owner_ = 0;
  pause_.release();
  return {result == InstallResult::ok ? SwitchStatus::complete
                                      : SwitchStatus::install_failed,
          result, installed.slot_touched};
}

}  // namespace firmingo_managed
