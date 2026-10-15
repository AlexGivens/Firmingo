#include "stateful_image.h"

#include "image.h"

#include <cstring>
#include <limits>

namespace firmingo_managed {
namespace stateful {
namespace {

std::uint16_t u16(const std::uint8_t* p) {
  return std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8);
}

std::uint32_t u32(const std::uint8_t* p) {
  return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
         (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

Result failed(Error error) { return {error, {}}; }

bool valid_contract(const Contract& c) {
  // RP2040 main SRAM bounds exclude both scratch banks. Containment does not
  // establish that the resident has reserved this window; its port must do so.
  constexpr std::uint32_t start = 0x20000000, end = 0x20040000;
  return c.api_address >= 0x10000000 && c.api_address < kSlotAddress &&
         c.api_address % 4 == 0 && c.state_address >= start &&
         c.state_address < end && c.state_address % 4 == 0 &&
         c.state_capacity_bytes > 0 && c.state_capacity_bytes % 4 == 0 &&
         c.state_capacity_bytes <= end - c.state_address &&
         c.maximum_capsule_bytes >= kCodeOffset + 4 &&
         c.maximum_capsule_bytes <= kMaximumCapsuleBytes;
}

bool entry(std::uint32_t address, std::uint32_t bytes) {
  const std::uint32_t base = kSlotAddress + kCodeOffset;
  const std::uint32_t instruction = address & ~std::uint32_t(1);
  return (address & 1) && instruction >= base &&
         instruction - base < bytes && bytes - (instruction - base) >= 2;
}

bool hash_matches(const std::uint8_t* data, std::size_t size,
                  const std::uint8_t* expected) {
  std::uint8_t digest[32], difference = 0;
  sha256(data, size, digest);
  for (unsigned i = 0; i < 32; ++i) difference |= digest[i] ^ expected[i];
  return difference == 0;
}

}  // namespace

Result validate_image(const std::uint8_t* image, std::size_t size,
                      const Contract& contract) {
  if (!valid_contract(contract)) return failed(Error::invalid_contract);
  if (!image || size < kCodeOffset) return failed(Error::short_header);
  if (size > contract.maximum_capsule_bytes) return failed(Error::bad_image_size);
  if (std::memcmp(image, "FMS2", 4)) return failed(Error::bad_magic);
  if (u16(image + 4) != kImageVersion) return failed(Error::unsupported_version);
  if (u16(image + 6) != kHeaderBytes) return failed(Error::bad_header_size);
  if (u32(image + 8) != kNanoBoardTag) return failed(Error::wrong_board);
  if (u32(image + 12) != contract.api_address) return failed(Error::wrong_api);
  if (u32(image + 16) != kSlotAddress + kCodeOffset)
    return failed(Error::wrong_code_address);
  const std::uint32_t code = u32(image + 20);
  if (!code || code % 4 || code > contract.maximum_capsule_bytes - kCodeOffset)
    return failed(Error::bad_code_size);
  const std::uint32_t setup = u32(image + 24), loop = u32(image + 28);
  if (!entry(setup, code) || !entry(loop, code)) return failed(Error::bad_entry);
  const std::uint32_t data = u32(image + 36), bss = u32(image + 44);
  if (u32(image + 32) != contract.state_address || data % 4 || bss % 4 ||
      data > contract.state_capacity_bytes || bss > contract.state_capacity_bytes - data ||
      u32(image + 40) != contract.state_address + data)
    return failed(Error::bad_state_layout);
  const std::uint32_t load = kCodeOffset + code;
  if (u32(image + 48) != load || data > contract.maximum_capsule_bytes - load)
    return failed(Error::bad_data_layout);
  const std::uint32_t total = load + data;
  if (u32(image + 52) != total || size != total) return failed(Error::bad_image_size);
  if (u32(image + 56) || u32(image + 60)) return failed(Error::nonzero_reserved);
  for (std::uint32_t i = kHeaderBytes; i < kCodeOffset; ++i)
    if (image[i]) return failed(Error::nonzero_reserved);
  // Header hash covers metadata and the payload hash. No self-referential field
  // or unauthenticated RAM/layout field is omitted from these integrity checks.
  if (!hash_matches(image, 96, image + 96)) return failed(Error::header_hash_mismatch);
  if (!hash_matches(image + kCodeOffset, total - kCodeOffset, image + 64))
    return failed(Error::payload_hash_mismatch);
  return {Error::ok, {total, code, setup, loop, load, data, bss}};
}

Result Generation::begin(const std::uint8_t* image, std::size_t size,
                         const Contract& contract, std::uint32_t generation) {
  if (active_) return failed(Error::active_generation);
  if (!generation || generation <= last_generation_) return failed(Error::bad_generation);
  const auto checked = validate_image(image, size, contract);
  if (checked.error != Error::ok) return checked;
  if (!arena_.data || arena_.bytes != contract.state_capacity_bytes ||
      arena_.mapped_address != contract.state_address)
    return failed(Error::wrong_arena);
  const auto source = reinterpret_cast<std::uintptr_t>(image);
  const auto destination = reinterpret_cast<std::uintptr_t>(arena_.data);
  const auto maximum = std::numeric_limits<std::uintptr_t>::max();
  if (source > maximum - size || destination > maximum - arena_.bytes ||
      (source <= destination ? destination - source < size
                             : source - destination < arena_.bytes))
    return failed(Error::overlapping_storage);
  // Every possible failure precedes the first write. Clear unused tail bytes too
  // so a smaller replacement cannot observe the preceding generation's state.
  std::memset(arena_.data, 0, arena_.bytes);
  if (checked.image.data_bytes)
    std::memcpy(arena_.data, image + checked.image.data_load_offset,
                checked.image.data_bytes);
  last_generation_ = generation;
  active_ = true;
  return checked;
}

}  // namespace stateful
}  // namespace firmingo_managed
