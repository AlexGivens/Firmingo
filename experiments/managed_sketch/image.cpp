#include "image.h"

#include <cstring>

namespace firmingo_managed {
namespace {

std::uint16_t u16(const std::uint8_t* p) {
  return std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8);
}

std::uint32_t u32(const std::uint8_t* p) {
  return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
         (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

std::uint32_t rotate(std::uint32_t x, unsigned n) {
  return (x >> n) | (x << (32 - n));
}

constexpr std::uint32_t k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

void block(const std::uint8_t* input, std::uint32_t state[8]) {
  std::uint32_t words[64];
  for (unsigned i = 0; i < 16; ++i) {
    const std::uint8_t* p = input + 4 * i;
    words[i] = (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
               (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
  }
  for (unsigned i = 16; i < 64; ++i) {
    const std::uint32_t s0 = rotate(words[i - 15], 7) ^ rotate(words[i - 15], 18) ^
                             (words[i - 15] >> 3);
    const std::uint32_t s1 = rotate(words[i - 2], 17) ^ rotate(words[i - 2], 19) ^
                             (words[i - 2] >> 10);
    words[i] = words[i - 16] + s0 + words[i - 7] + s1;
  }
  std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
  std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
  for (unsigned i = 0; i < 64; ++i) {
    const std::uint32_t s1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
    const std::uint32_t choice = (e & f) ^ (~e & g);
    const std::uint32_t t1 = h + s1 + choice + k[i] + words[i];
    const std::uint32_t s0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
    const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t t2 = s0 + majority;
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2;
  }
  state[0] += a; state[1] += b; state[2] += c; state[3] += d;
  state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

bool entry_in_code(std::uint32_t address, std::uint32_t code_start,
                   std::uint32_t code_bytes) {
  if ((address & 1u) == 0) return false;  // Cortex-M Thumb function pointer.
  const std::uint32_t instruction_address = address & ~std::uint32_t(1);
  return instruction_address >= code_start &&
         instruction_address - code_start < code_bytes;
}

}  // namespace

void sha256(const std::uint8_t* data, std::size_t size, std::uint8_t out[32]) {
  std::uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  const std::size_t original_size = size;
  while (size >= 64) {
    block(data, state);
    data += 64;
    size -= 64;
  }
  std::uint8_t tail[128] = {};
  if (size) std::memcpy(tail, data, size);
  tail[size] = 0x80;
  const std::uint64_t bit_count = std::uint64_t(original_size) * 8;
  const std::size_t final_size = size >= 56 ? 128 : 64;
  for (unsigned i = 0; i < 8; ++i)
    tail[final_size - 8 + i] = std::uint8_t(bit_count >> (56 - 8 * i));
  block(tail, state);
  if (final_size == 128) block(tail + 64, state);
  for (unsigned i = 0; i < 8; ++i) {
    out[4 * i] = std::uint8_t(state[i] >> 24);
    out[4 * i + 1] = std::uint8_t(state[i] >> 16);
    out[4 * i + 2] = std::uint8_t(state[i] >> 8);
    out[4 * i + 3] = std::uint8_t(state[i]);
  }
}

ImageError validate_image(const std::uint8_t* image, std::size_t available,
                          std::uint32_t expected_api_address,
                          ImageInfo* info) {
  if (!image || available < kCodeOffset) return ImageError::short_header;
  if (std::memcmp(image, "FMS1", 4) != 0) return ImageError::bad_magic;
  if (u16(image + 4) != kAbiVersion) return ImageError::unsupported_abi;
  if (u16(image + 6) != kHeaderBytes) return ImageError::bad_header_size;
  if (u32(image + 8) != kNanoBoardTag) return ImageError::wrong_board;
  if (u32(image + 12) != expected_api_address) return ImageError::wrong_api;
  const std::uint32_t code_address = kSlotAddress + kCodeOffset;
  if (u32(image + 16) != code_address) return ImageError::wrong_code_address;
  const std::uint32_t code_bytes = u32(image + 20);
  if (!code_bytes || code_bytes > kSlotBytes - kCodeOffset)
    return ImageError::bad_size;
  const std::uint32_t setup_address = u32(image + 24);
  const std::uint32_t loop_address = u32(image + 28);
  if (!entry_in_code(setup_address, code_address, code_bytes) ||
      !entry_in_code(loop_address, code_address, code_bytes))
    return ImageError::bad_entry;
  for (std::uint32_t i = kHeaderBytes; i < kCodeOffset; ++i)
    if (image[i]) return ImageError::nonzero_padding;
  if (available - kCodeOffset < code_bytes) return ImageError::short_image;
  std::uint8_t digest[32];
  sha256(image + kCodeOffset, code_bytes, digest);
  std::uint8_t difference = 0;
  for (unsigned i = 0; i < 32; ++i) difference |= digest[i] ^ image[32 + i];
  if (difference) return ImageError::hash_mismatch;
  if (info) {
    info->code_bytes = code_bytes;
    info->setup_address = setup_address;
    info->loop_address = loop_address;
  }
  return ImageError::ok;
}

}  // namespace firmingo_managed
