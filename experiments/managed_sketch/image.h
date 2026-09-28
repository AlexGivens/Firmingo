#pragma once

#include <cstddef>
#include <cstdint>

namespace firmingo_managed {

// Experimental Nano image format. Only the opt-in M9 resident advertises it;
// the 0.1.0 release references have no upload backend or target.
constexpr std::uint32_t kNanoBoardTag = 0x4f4e414e;  // "NANO" in little endian
constexpr std::uint16_t kAbiVersion = 1;
constexpr std::uint32_t kSlotAddress = 0x10200000;
constexpr std::uint32_t kSlotBytes = 4 * 1024 * 1024;
constexpr std::uint32_t kCodeOffset = 256;
constexpr std::uint32_t kHeaderBytes = 64;

enum class ImageError {
  ok,
  short_header,
  bad_magic,
  unsupported_abi,
  bad_header_size,
  wrong_board,
  wrong_api,
  wrong_code_address,
  bad_size,
  bad_entry,
  nonzero_padding,
  short_image,
  hash_mismatch,
};

struct ImageInfo {
  std::uint32_t code_bytes;
  std::uint32_t setup_address;
  std::uint32_t loop_address;
};

// The API address comes from the exact resident ELF for this experimental
// build. Matching that address is not authentication of a network peer.
ImageError validate_image(const std::uint8_t* image, std::size_t available,
                          std::uint32_t expected_api_address,
                          ImageInfo* info);

// Exposed for a known-vector test and for the image builder's cross-check.
void sha256(const std::uint8_t* data, std::size_t size, std::uint8_t out[32]);

}  // namespace firmingo_managed
