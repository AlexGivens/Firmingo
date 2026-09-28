#include "install.h"

#include <cstring>

namespace firmingo_managed {

InstallReport install_verified_capsule(const CapsuleStage& stage,
                                       std::uint32_t owner,
                                       std::uint32_t api_address,
                                       ProofFlashPort& flash) {
  std::size_t size = 0;
  const std::uint8_t* capsule = stage.verified(owner, &size);
  if (!capsule) return {InstallResult::no_verified_capsule, false};
  ImageInfo info{};
  if (size > kProofSectorBytes || size <= kCodeOffset ||
      validate_image(capsule, size, api_address, &info) != ImageError::ok ||
      size != kCodeOffset + info.code_bytes)
    return {InstallResult::invalid_image, false};
  if (!flash.begin()) return {InstallResult::flash_begin_failed, false};

  // All addresses are fixed by the Nano proof layout; no network-supplied
  // offset can reach the resident, staging, metadata, or EEPROM regions.
  constexpr std::uint32_t slot_offset = kSlotAddress - kFlashXipAddress;
  InstallResult result = InstallResult::ok;
  if (!flash.erase(slot_offset, kProofSectorBytes)) {
    result = InstallResult::erase_failed;
  } else {
    std::uint8_t page[kProofPageBytes];
    for (std::size_t offset = 0; offset < size; offset += kProofPageBytes) {
      std::memset(page, 0xff, sizeof(page));
      const std::size_t count = size - offset < sizeof(page)
                                    ? size - offset : sizeof(page);
      std::memcpy(page, capsule + offset, count);
      if (!flash.program(slot_offset + offset, page, sizeof(page))) {
        result = InstallResult::program_failed;
        break;
      }
    }
  }
  flash.end();
  if (result != InstallResult::ok) return {result, true};
  const std::uint8_t* written = flash.slot_data();
  if (!written || std::memcmp(written, capsule, size) != 0 ||
      validate_image(written, kProofSectorBytes, api_address, nullptr) != ImageError::ok)
    return {InstallResult::readback_failed, true};
  return {InstallResult::ok, true};
}

}  // namespace firmingo_managed
