#include "image.h"
#include "unity.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <iostream>
#include <vector>

using namespace firmingo_managed;

void setUp() {}
void tearDown() {}

namespace {
constexpr std::uint32_t kApiAddress = 0x10020200;

void put16(std::vector<std::uint8_t>& image, std::size_t at, std::uint16_t value) {
  image[at] = std::uint8_t(value);
  image[at + 1] = std::uint8_t(value >> 8);
}

void put32(std::vector<std::uint8_t>& image, std::size_t at, std::uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) image[at + i] = std::uint8_t(value >> (8 * i));
}

std::vector<std::uint8_t> image_with_code() {
  std::vector<std::uint8_t> image(kCodeOffset + 300);
  std::memcpy(image.data(), "FMS1", 4);
  put16(image, 4, kAbiVersion);
  put16(image, 6, kHeaderBytes);
  put32(image, 8, kNanoBoardTag);
  put32(image, 12, kApiAddress);
  put32(image, 16, kSlotAddress + kCodeOffset);
  put32(image, 20, 300);
  put32(image, 24, kSlotAddress + kCodeOffset + 1);
  put32(image, 28, kSlotAddress + kCodeOffset + 101);
  for (std::size_t i = kCodeOffset; i < image.size(); ++i)
    image[i] = std::uint8_t((i * 73u) ^ (i >> 3));
  sha256(image.data() + kCodeOffset, 300, image.data() + 32);
  return image;
}

void expect_error(const std::vector<std::uint8_t>& image, ImageError expected) {
  TEST_ASSERT_EQUAL_INT(int(expected),
                        int(validate_image(image.data(), image.size(), kApiAddress, nullptr)));
}
}  // namespace

void sha256_matches_published_empty_and_abc_vectors() {
  const std::array<std::uint8_t,32> empty = {
      0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
      0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55};
  const std::array<std::uint8_t,32> abc = {
      0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
      0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
  std::uint8_t out[32];
  sha256(nullptr, 0, out);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(empty.data(), out, 32);
  sha256(reinterpret_cast<const std::uint8_t*>("abc"), 3, out);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(abc.data(), out, 32);
}

void sha256_handles_padding_boundaries_and_multiple_blocks() {
  const std::size_t lengths[] = {55, 56, 64, 65};
  const char* expected[] = {
      "463eb28e72f82e0a96c0a4cc53690c571281131f672aa229e0d45ae59b598b59",
      "da2ae4d6b36748f2a318f23e7ab1dfdf45acdc9d049bd80e59de82a60895f562",
      "fdeab9acf3710362bd2658cdc9a29e8f9c757fcf9811603a8c447cd1d9151108",
      "4bfd2c8b6f1eec7a2afeb48b934ee4b2694182027e6d0fc075074f2fabb31781"};
  std::uint8_t input[65], digest[32];
  const char digits[] = "0123456789abcdef";
  for (unsigned i = 0; i < 65; ++i) input[i] = std::uint8_t(i);
  for (unsigned case_index = 0; case_index < 4; ++case_index) {
    sha256(input, lengths[case_index], digest);
    char actual[65];
    for (unsigned i = 0; i < 32; ++i) {
      actual[2 * i] = digits[digest[i] >> 4];
      actual[2 * i + 1] = digits[digest[i] & 15];
    }
    actual[64] = 0;
    TEST_ASSERT_EQUAL_STRING(expected[case_index], actual);
  }
}

void valid_image_reports_bounded_thumb_entries() {
  const auto image = image_with_code();
  ImageInfo info{};
  TEST_ASSERT_EQUAL_INT(int(ImageError::ok),
                        int(validate_image(image.data(), image.size(), kApiAddress, &info)));
  TEST_ASSERT_EQUAL_UINT32(300, info.code_bytes);
  TEST_ASSERT_EQUAL_HEX32(kSlotAddress + kCodeOffset + 1, info.setup_address);
  TEST_ASSERT_EQUAL_HEX32(kSlotAddress + kCodeOffset + 101, info.loop_address);
}

void wrong_target_or_abi_never_validates() {
  auto image = image_with_code();
  put16(image, 4, 2);
  expect_error(image, ImageError::unsupported_abi);
  image = image_with_code(); put32(image, 8, 0);
  expect_error(image, ImageError::wrong_board);
  image = image_with_code(); put32(image, 12, 0);
  expect_error(image, ImageError::wrong_api);
  image = image_with_code(); put32(image, 16, kSlotAddress);
  expect_error(image, ImageError::wrong_code_address);
}

void oversized_truncated_and_invalid_entries_are_rejected_before_hashing() {
  auto image = image_with_code();
  put32(image, 20, kSlotBytes);
  expect_error(image, ImageError::bad_size);
  image = image_with_code(); image.resize(image.size() - 1);
  expect_error(image, ImageError::short_image);
  image = image_with_code(); put32(image, 24, kSlotAddress + kCodeOffset);
  expect_error(image, ImageError::bad_entry);
  image = image_with_code(); put32(image, 28, kSlotAddress + kCodeOffset + 301);
  expect_error(image, ImageError::bad_entry);
}

void changed_code_or_padding_is_rejected_without_side_effects() {
  auto image = image_with_code();
  image[kCodeOffset + 17] ^= 0x80;
  expect_error(image, ImageError::hash_mismatch);
  image = image_with_code(); image[64] = 1;
  expect_error(image, ImageError::nonzero_padding);
}

int main(int argc, char** argv) {
  if (argc == 3) {
    std::ifstream file(argv[1], std::ios::binary);
    if (!file) return 2;
    const std::vector<std::uint8_t> image(std::istreambuf_iterator<char>{file},
                                          std::istreambuf_iterator<char>{});
    const std::uint32_t api_address = std::uint32_t(std::strtoul(argv[2], nullptr, 0));
    ImageInfo info{};
    const ImageError result = validate_image(image.data(), image.size(), api_address, &info);
    if (result != ImageError::ok) {
      std::cerr << "image validation failed: " << int(result) << '\n';
      return 1;
    }
    std::cout << "valid managed image: " << info.code_bytes << " code bytes\n";
    return 0;
  }
  if (argc != 1) return 2;
  UNITY_BEGIN();
  RUN_TEST(sha256_matches_published_empty_and_abc_vectors);
  RUN_TEST(sha256_handles_padding_boundaries_and_multiple_blocks);
  RUN_TEST(valid_image_reports_bounded_thumb_entries);
  RUN_TEST(wrong_target_or_abi_never_validates);
  RUN_TEST(oversized_truncated_and_invalid_entries_are_rejected_before_hashing);
  RUN_TEST(changed_code_or_padding_is_rejected_without_side_effects);
  return UNITY_END();
}
