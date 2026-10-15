#include "stateful_runtime.h"

#include <cstring>
#include <limits>

namespace firmingo_managed {
namespace stateful {
namespace {
Result runtime_failed(Error error) { return {error, {}}; }
std::uint32_t slot_u32(const std::uint8_t* p) {
  return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
         (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
}  // namespace

bool Runtime::can_start() const {
  return generation_.last_generation() != std::numeric_limits<std::uint32_t>::max();
}

Result Runtime::start_exact(const std::uint8_t* image, std::size_t bytes) {
  if (!can_start()) return runtime_failed(Error::bad_generation);
  const auto next = generation_.last_generation() + 1;
  const auto result = generation_.begin(image, bytes, contract_, next);
  if (result.error != Error::ok) return result;
  image_ = result.image;
  returned_.store(0, std::memory_order_release);
  // Initialization and entry metadata happen before core 1 can acquire a token.
  published_.store(next, std::memory_order_release);
  return result;
}

Result Runtime::start_from_slot(const std::uint8_t* slot, std::size_t mapped_bytes) {
  // Never pass the entire erased slot to the exact-span validator. The length
  // field itself is read only inside a mapped 256-byte prefix, then bounded
  // against both trusted limits before any hash, copy or entry publication.
  if (!slot || mapped_bytes < kCodeOffset) return runtime_failed(Error::short_header);
  const std::uint32_t bytes = slot_u32(slot + 52);
  if (bytes < kCodeOffset || bytes > mapped_bytes ||
      bytes > contract_.maximum_capsule_bytes || bytes > kMaximumCapsuleBytes)
    return runtime_failed(Error::bad_image_size);
  return start_exact(slot, bytes);
}

Result Runtime::boot_from_slot(const std::uint8_t* slot, std::size_t mapped_bytes) {
  if (boot_attempted_) return runtime_failed(Error::bad_generation);
  boot_attempted_ = true;
  return start_from_slot(slot, mapped_bytes);
}

void Runtime::stop_quiescent() {
  published_.store(0, std::memory_order_release);
  returned_.store(0, std::memory_order_release);
  generation_.stop();
  image_ = {};
  port_.stopped();
}

bool Runtime::tick() {
  const auto current = published_.load(std::memory_order_acquire);
  if (!current) {
    observed_ = 0;
    return false;
  }
  if (observed_ != current) {
    observed_ = current;
    port_.invoke(image_.setup_address);
  }
  port_.invoke(image_.loop_address);
  returned_.store(current, std::memory_order_release);
  return true;
}

bool Runtime::loop_returned() const {
  const auto current = published_.load(std::memory_order_acquire);
  return current && returned_.load(std::memory_order_acquire) == current;
}

bool Replacement::request(const std::uint8_t* image, std::size_t bytes,
                           bool physically_armed, std::uint32_t now) {
  if (active() || !physically_armed || !runtime_.boot_attempted_ ||
      !runtime_.can_start() || !image || bytes > sizeof(snapshot_) ||
      validate_image(image, bytes, runtime_.contract_).error != Error::ok)
    return false;
  std::memcpy(snapshot_, image, bytes);
  if (validate_image(snapshot_, bytes, runtime_.contract_).error != Error::ok ||
      !pause_.request()) {
    std::memset(snapshot_, 0, sizeof(snapshot_));
    return false;
  }
  bytes_ = bytes;
  started_at_ = now;
  return true;
}

void Replacement::release() {
  std::memset(snapshot_, 0, sizeof(snapshot_));
  bytes_ = 0;
  pause_.release();
}

InstallReport Replacement::install() {
  if (!flash_.begin()) return {InstallResult::flash_begin_failed, false};
  constexpr std::uint32_t offset = kSlotAddress - kFlashXipAddress;
  InstallResult result = InstallResult::ok;
  if (!flash_.erase(offset, kProofSectorBytes)) {
    result = InstallResult::erase_failed;
  } else {
    std::uint8_t page[kProofPageBytes];
    for (std::size_t at = 0; at < bytes_; at += sizeof(page)) {
      std::memset(page, 0xff, sizeof(page));
      const auto count = bytes_ - at < sizeof(page) ? bytes_ - at : sizeof(page);
      std::memcpy(page, snapshot_ + at, count);
      if (!flash_.program(offset + at, page, sizeof(page))) {
        result = InstallResult::program_failed;
        break;
      }
    }
  }
  flash_.end();
  if (result != InstallResult::ok) return {result, true};
  const auto* slot = flash_.slot_data();
  if (!slot || std::memcmp(slot, snapshot_, bytes_) ||
      validate_image(slot, bytes_, runtime_.contract_).error != Error::ok)
    return {InstallResult::readback_failed, true};
  // The whole first sector must have the selected contents, including erased
  // tail. These bounds are fixed by the port contract, never an image address.
  for (std::size_t at = bytes_; at < kProofSectorBytes; ++at)
    if (slot[at] != 0xff) return {InstallResult::readback_failed, true};
  return {InstallResult::ok, true};
}

ReplacementReport Replacement::poll(std::uint32_t now, bool physically_armed) {
  if (!active()) return {ReplacementStatus::idle, InstallResult::no_verified_capsule, Error::ok, false};
  if (!physically_armed) {
    release();
    return {ReplacementStatus::denied, InstallResult::no_verified_capsule, Error::ok, false};
  }
  if (!pause_.parked()) {
    if (static_cast<std::uint32_t>(now - started_at_) < pause_timeout_ms)
      return {ReplacementStatus::waiting, InstallResult::no_verified_capsule, Error::ok, false};
    release();
    return {ReplacementStatus::timeout, InstallResult::no_verified_capsule, Error::ok, false};
  }
  const auto checked = validate_image(snapshot_, bytes_, runtime_.contract_);
  if (checked.error != Error::ok) {
    release();
    return {ReplacementStatus::failed, InstallResult::invalid_image, checked.error, false};
  }
  runtime_.stop_quiescent();
  const auto written = install();
  auto started = runtime_failed(Error::ok);
  if (written.result == InstallResult::ok)
    started = runtime_.start_from_slot(flash_.slot_data(), kProofSectorBytes);
  const bool success = written.result == InstallResult::ok && started.error == Error::ok;
  release();  // Always release core 1, even with a partial slot and no active sketch.
  return {success ? ReplacementStatus::complete : ReplacementStatus::failed,
          written.result, started.error, written.slot_touched};
}

}  // namespace stateful
}  // namespace firmingo_managed
