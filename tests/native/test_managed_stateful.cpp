#include "stateful_image.h"
#include "image.h"
#include "unity.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

namespace sf = firmingo_managed::stateful;
void setUp() {}
void tearDown() {}

namespace {
constexpr sf::Contract contract{0x10000100, 0x20030000, 32, 4096};

std::vector<std::uint8_t> fixture() {
  std::ifstream input(STATEFUL_FIXTURE_DIR "/initialized.hex");
  TEST_ASSERT_TRUE(input.good());
  std::vector<std::uint8_t> bytes;
  unsigned value;
  while (input >> std::hex >> value) {
    TEST_ASSERT_TRUE(value <= 255);
    bytes.push_back(static_cast<std::uint8_t>(value));
  }
  TEST_ASSERT_EQUAL_UINT(272, bytes.size());
  return bytes;
}

void put32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) bytes[at + i] = value >> (8 * i);
}

// Variant/mutation signing is separate from the immutable hashlib-authored golden.
void sign(std::vector<std::uint8_t>& bytes) {
  firmingo_managed::sha256(bytes.data() + 256, bytes.size() - 256, bytes.data() + 64);
  firmingo_managed::sha256(bytes.data(), 96, bytes.data() + 96);
}

struct Memory {
  std::array<std::uint8_t, 48> bytes;
  Memory() { bytes.fill(0xa5); }
  sf::Arena arena() { return {bytes.data() + 8, 32, contract.state_address}; }
  void guards() const {
    for (unsigned i = 0; i < 8; ++i) {
      TEST_ASSERT_EQUAL_HEX8(0xa5, bytes[i]);
      TEST_ASSERT_EQUAL_HEX8(0xa5, bytes[40 + i]);
    }
  }
  void unchanged() const {
    for (auto byte : bytes) TEST_ASSERT_EQUAL_HEX8(0xa5, byte);
  }
};

void zero_result(const sf::Result& result) {
  const std::uint32_t fields[] = {result.image.image_bytes, result.image.code_bytes,
      result.image.setup_address, result.image.loop_address, result.image.data_load_offset,
      result.image.data_bytes, result.image.bss_bytes};
  for (auto value : fields) TEST_ASSERT_EQUAL_UINT32(0, value);
}

void rejects(const std::vector<std::uint8_t>& bytes, sf::Error expected) {
  const auto parsed = sf::validate_image(bytes.data(), bytes.size(), contract);
  TEST_ASSERT_EQUAL_INT(int(expected), int(parsed.error));
  zero_result(parsed);
  Memory memory;
  sf::Generation runtime(memory.arena());
  const auto started = runtime.begin(bytes.data(), bytes.size(), contract, 1);
  TEST_ASSERT_EQUAL_INT(int(expected), int(started.error));
  zero_result(started);
  TEST_ASSERT_FALSE(runtime.active());
  TEST_ASSERT_EQUAL_UINT32(0, runtime.last_generation());
  memory.unchanged();
}
}  // namespace

void independent_golden_validates_and_old_format_still_rejects_it() {
  const auto bytes = fixture();
  std::uint8_t digest[32];
  firmingo_managed::sha256(bytes.data(), bytes.size(), digest);
  const std::uint8_t expected[] = {0x1c,0x95,0x5e,0x83,0x2d,0x35,0x1a,0x9b,
      0x91,0xa3,0xbd,0x5e,0x5e,0x44,0x63,0xa2,0xf6,0x73,0x2e,0x4e,0x87,0xcd,
      0x1d,0xe0,0xd7,0x83,0xc7,0x69,0xf0,0x1a,0xe8,0x67};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, digest, 32);
  const auto result = sf::validate_image(bytes.data(), bytes.size(), contract);
  TEST_ASSERT_EQUAL_INT(int(sf::Error::ok), int(result.error));
  TEST_ASSERT_EQUAL_UINT32(272, result.image.image_bytes);
  TEST_ASSERT_EQUAL_UINT32(264, result.image.data_load_offset);
  TEST_ASSERT_EQUAL_UINT32(8, result.image.data_bytes);
  TEST_ASSERT_EQUAL_UINT32(8, result.image.bss_bytes);
  TEST_ASSERT_EQUAL_HEX32(0x10200101, result.image.setup_address);
  TEST_ASSERT_EQUAL_INT(int(firmingo_managed::ImageError::bad_magic),
      int(firmingo_managed::validate_image(bytes.data(), bytes.size(), contract.api_address, nullptr)));
  auto old_magic = bytes; old_magic[3] = '1';
  rejects(old_magic, sf::Error::bad_magic);
}

void initialization_copies_data_zeroes_bss_and_scrubs_unused_arena() {
  const auto bytes = fixture();
  Memory memory;
  sf::Generation runtime(memory.arena());
  const auto result = runtime.begin(bytes.data(), bytes.size(), contract, 1);
  TEST_ASSERT_EQUAL_INT(int(sf::Error::ok), int(result.error));
  TEST_ASSERT_TRUE(runtime.active());
  TEST_ASSERT_EQUAL_UINT32(1, runtime.last_generation());
  const std::uint8_t data[] = {0x44,0x33,0x22,0x11,0xdd,0xcc,0xbb,0xaa};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(data, memory.arena().data, 8);
  for (unsigned i = 8; i < 32; ++i) TEST_ASSERT_EQUAL_HEX8(0, memory.arena().data[i]);
  memory.guards();
}

void active_generation_retains_loop_state_and_requires_explicit_stop() {
  const auto bytes = fixture();
  Memory memory;
  sf::Generation runtime(memory.arena());
  TEST_ASSERT_EQUAL_INT(int(sf::Error::ok), int(runtime.begin(bytes.data(), bytes.size(), contract, 10).error));
  // Host writes model sketch-owned mutable RAM, not execution of synthetic Thumb code.
  ++memory.arena().data[0]; memory.arena().data[8] = 23;
  const auto saved = memory.bytes;
  for (std::uint32_t generation : {10u, 11u}) {
    TEST_ASSERT_EQUAL_INT(int(sf::Error::active_generation),
        int(runtime.begin(bytes.data(), bytes.size(), contract, generation).error));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(saved.data(), memory.bytes.data(), saved.size());
  }
  runtime.stop();
  TEST_ASSERT_FALSE(runtime.active());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(saved.data(), memory.bytes.data(), saved.size());
  for (std::uint32_t generation : {0u, 9u, 10u}) {
    TEST_ASSERT_EQUAL_INT(int(sf::Error::bad_generation),
        int(runtime.begin(bytes.data(), bytes.size(), contract, generation).error));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(saved.data(), memory.bytes.data(), saved.size());
  }
  TEST_ASSERT_EQUAL_INT(int(sf::Error::ok), int(runtime.begin(bytes.data(), bytes.size(), contract, 11).error));
  TEST_ASSERT_EQUAL_HEX8(0x44, memory.arena().data[0]);
  TEST_ASSERT_EQUAL_HEX8(0, memory.arena().data[8]);
  memory.guards();
}

void smaller_replacement_and_failed_start_do_not_leak_or_reinitialize_old_state() {
  const auto original = fixture();
  Memory memory;
  sf::Generation runtime(memory.arena());
  TEST_ASSERT_EQUAL_INT(int(sf::Error::ok), int(runtime.begin(original.data(), original.size(), contract, 1).error));
  std::memset(memory.arena().data, 0x6b, 32);
  runtime.stop();
  auto corrupt = original; corrupt[264] ^= 1;
  const auto saved = memory.bytes;
  TEST_ASSERT_EQUAL_INT(int(sf::Error::payload_hash_mismatch),
      int(runtime.begin(corrupt.data(), corrupt.size(), contract, 2).error));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(saved.data(), memory.bytes.data(), saved.size());
  TEST_ASSERT_EQUAL_UINT32(1, runtime.last_generation());
  TEST_ASSERT_FALSE(runtime.active());
  auto smaller = original; smaller.resize(268);
  put32(smaller, 36, 4); put32(smaller, 40, contract.state_address + 4);
  put32(smaller, 44, 0); put32(smaller, 52, 268); sign(smaller);
  TEST_ASSERT_EQUAL_INT(int(sf::Error::ok), int(runtime.begin(smaller.data(), smaller.size(), contract, 2).error));
  for (unsigned i = 4; i < 32; ++i) TEST_ASSERT_EQUAL_HEX8(0, memory.arena().data[i]);
  memory.guards();
}

void all_truncations_trailing_bytes_and_null_input_reject_without_ram_writes() {
  const auto original = fixture();
  for (std::size_t n = 0; n < original.size(); ++n) {
    Memory memory; sf::Generation runtime(memory.arena());
    const auto result = runtime.begin(original.data(), n, contract, 1);
    TEST_ASSERT_TRUE(result.error != sf::Error::ok); zero_result(result);
    memory.unchanged();
  }
  auto extra = original; extra.push_back(0xff);
  rejects(extra, sf::Error::bad_image_size);
  auto excessive = original; excessive.resize(4097);
  rejects(excessive, sf::Error::bad_image_size);
  TEST_ASSERT_EQUAL_INT(int(sf::Error::short_header), int(sf::validate_image(nullptr, 272, contract).error));
  TEST_ASSERT_EQUAL_INT(int(sf::Error::bad_image_size),
      int(sf::validate_image(original.data(), std::numeric_limits<std::size_t>::max(), contract).error));
}

void metadata_bounds_reject_even_when_hashes_are_recomputed() {
  struct Case { unsigned offset; std::uint32_t value; sf::Error error; };
  const Case cases[] = {
      {4, 3u | (128u << 16), sf::Error::unsupported_version},
      {4, 2u | (64u << 16), sf::Error::bad_header_size},
      {8, 0, sf::Error::wrong_board}, {12, 0x10000104, sf::Error::wrong_api},
      {16, 0x10200000, sf::Error::wrong_code_address},
      {20, 0, sf::Error::bad_code_size}, {20, 7, sf::Error::bad_code_size},
      {20, UINT32_MAX, sf::Error::bad_code_size},
      {24, 0x10200100, sf::Error::bad_entry}, {28, 0x10200109, sf::Error::bad_entry},
      {28, 0x1020010b, sf::Error::bad_entry}, {24, UINT32_MAX, sf::Error::bad_entry},
      {32, 0x20000000, sf::Error::bad_state_layout},
      {36, UINT32_MAX, sf::Error::bad_state_layout}, {36, 5, sf::Error::bad_state_layout},
      {36, UINT32_MAX - 3, sf::Error::bad_state_layout},
      {40, 0x2003000c, sf::Error::bad_state_layout},
      {44, UINT32_MAX - 3, sf::Error::bad_state_layout},
      {44, 28, sf::Error::bad_state_layout}, {44, 7, sf::Error::bad_state_layout},
      {48, UINT32_MAX, sf::Error::bad_data_layout},
      {52, UINT32_MAX, sf::Error::bad_image_size},
      {56, 1, sf::Error::nonzero_reserved}, {60, 1, sf::Error::nonzero_reserved}};
  for (const auto& test : cases) {
    auto bytes = fixture(); put32(bytes, test.offset, test.value); sign(bytes);
    rejects(bytes, test.error);
  }
  auto bytes = fixture(); bytes[128] = 1; sign(bytes);
  rejects(bytes, sf::Error::nonzero_reserved);
  bytes = fixture(); put32(bytes,20,3840); put32(bytes,48,4096); sign(bytes);
  rejects(bytes, sf::Error::bad_data_layout);  // No flash room remains for data.
}

void header_and_payload_integrity_cover_entries_ram_and_initial_values() {
  auto bytes = fixture(); bytes[96] ^= 1; rejects(bytes, sf::Error::header_hash_mismatch);
  bytes = fixture(); put32(bytes, 24, 0x10200105); rejects(bytes, sf::Error::header_hash_mismatch);
  bytes = fixture(); put32(bytes, 44, 4); rejects(bytes, sf::Error::header_hash_mismatch);
  bytes = fixture(); bytes[64] ^= 1; rejects(bytes, sf::Error::header_hash_mismatch);
  bytes = fixture(); bytes[256] ^= 1; rejects(bytes, sf::Error::payload_hash_mismatch);
  bytes = fixture(); bytes[264] ^= 1; rejects(bytes, sf::Error::payload_hash_mismatch);
}

void contract_and_actual_arena_must_match_before_initialization() {
  const auto bytes = fixture();
  const sf::Contract invalid[] = {
      {0,0x20030000,32,4096}, {0x10000101,0x20030000,32,4096},
      {0x10200000,0x20030000,32,4096}, {0x10000100,0x20040000,32,4096},
      {0x10000100,0x2003fff0,32,4096}, {0x10000100,0x20030001,32,4096},
      {0x10000100,UINT32_MAX,32,4096}, {0x10000100,0x20030000,0,4096},
      {0x10000100,0x20030000,31,4096}, {0x10000100,0x20030000,UINT32_MAX,4096},
      {0x10000100,0x20030000,32,259}, {0x10000100,0x20030000,32,4097}};
  for (const auto& c : invalid) {
    Memory memory; sf::Generation runtime(memory.arena());
    TEST_ASSERT_EQUAL_INT(int(sf::Error::invalid_contract), int(runtime.begin(bytes.data(), bytes.size(), c, 1).error));
    memory.unchanged();
  }
  for (unsigned which = 0; which < 3; ++which) {
    Memory memory; auto arena = memory.arena();
    if (which == 0) arena.data = nullptr;
    if (which == 1) arena.bytes = 31;
    if (which == 2) arena.mapped_address += 4;
    sf::Generation runtime(arena);
    TEST_ASSERT_EQUAL_INT(int(sf::Error::wrong_arena), int(runtime.begin(bytes.data(), bytes.size(), contract, 1).error));
    memory.unchanged();
  }
}

void boundary_layouts_allow_zero_sections_and_the_exact_capsule_and_ram_limits() {
  for (const auto data : {0u, 16u, 32u}) {
    auto bytes = fixture(); bytes.resize(264 + data, 0x39);
    put32(bytes, 36, data); put32(bytes, 40, contract.state_address + data);
    put32(bytes, 44, 32 - data); put32(bytes, 52, bytes.size()); sign(bytes);
    Memory memory; sf::Generation runtime(memory.arena());
    TEST_ASSERT_EQUAL_INT(int(sf::Error::ok), int(runtime.begin(bytes.data(), bytes.size(), contract, 1).error));
    if (data) TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data() + 264, memory.arena().data, data);
    for (unsigned i = data; i < 32; ++i) TEST_ASSERT_EQUAL_HEX8(0, memory.arena().data[i]);
    memory.guards();
  }
  auto maximum = fixture(); maximum.resize(4096, 0x77);
  put32(maximum, 20, 3832); put32(maximum, 48, 4088); put32(maximum, 52, 4096);
  put32(maximum, 28, 0x10200ff7); sign(maximum);
  TEST_ASSERT_EQUAL_INT(int(sf::Error::ok), int(sf::validate_image(maximum.data(), maximum.size(), contract).error));
  auto end = fixture(); put32(end, 32, 0x2003ffe0); put32(end, 40, 0x2003ffe8); sign(end);
  const sf::Contract at_end{contract.api_address,0x2003ffe0,32,4096};
  TEST_ASSERT_EQUAL_INT(int(sf::Error::ok), int(sf::validate_image(end.data(), end.size(), at_end).error));
  auto minimum = fixture(); minimum.resize(260);
  put32(minimum,20,4); put32(minimum,28,0x10200101); put32(minimum,36,0);
  put32(minimum,40,contract.state_address); put32(minimum,44,0);
  put32(minimum,48,260); put32(minimum,52,260); sign(minimum);
  const sf::Contract at_minimum{contract.api_address,contract.state_address,32,260};
  Memory memory; sf::Generation runtime(memory.arena());
  TEST_ASSERT_EQUAL_INT(int(sf::Error::ok), int(runtime.begin(minimum.data(), minimum.size(), at_minimum, 1).error));
  for (unsigned i = 0; i < 32; ++i) TEST_ASSERT_EQUAL_HEX8(0, memory.arena().data[i]);
  memory.guards();
}

void aliasing_image_and_ram_and_generation_wrap_are_rejected() {
  auto bytes = fixture(); const auto saved = bytes;
  for (unsigned offset : {0u, 96u, 250u}) {
    sf::Generation runtime({bytes.data() + offset,32,contract.state_address});
    // Offset 250 aliases the capsule and extends past its view, but no write occurs.
    TEST_ASSERT_EQUAL_INT(int(sf::Error::overlapping_storage),
        int(runtime.begin(bytes.data(), bytes.size(), contract, 1).error));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(saved.data(), bytes.data(), bytes.size());
  }
  std::vector<std::uint8_t> backing(320, 0xa5);
  std::copy(bytes.begin(), bytes.end(), backing.begin() + 16);
  const auto backing_before = backing;
  sf::Generation preceding({backing.data(),32,contract.state_address});
  TEST_ASSERT_EQUAL_INT(int(sf::Error::overlapping_storage),
      int(preceding.begin(backing.data() + 16, bytes.size(), contract, 1).error));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(backing_before.data(), backing.data(), backing.size());
  Memory memory; sf::Generation runtime(memory.arena());
  TEST_ASSERT_EQUAL_INT(int(sf::Error::ok), int(runtime.begin(bytes.data(), bytes.size(), contract, UINT32_MAX).error));
  runtime.stop(); const auto before = memory.bytes;
  TEST_ASSERT_EQUAL_INT(int(sf::Error::bad_generation), int(runtime.begin(bytes.data(), bytes.size(), contract, 1).error));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(before.data(), memory.bytes.data(), before.size());
}

#if FIRMINGO_STATEFUL_FUZZ
void bounded_stateful_mutations_preserve_guards_and_failed_state() {
  std::uint32_t random = 0x46534d32;
  std::printf("FMS2 seed=0x%08x cases=20000\n", unsigned(random));
  const auto next = [&]() -> std::uint32_t {
    random ^= random << 13; random ^= random >> 17; random ^= random << 5; return random;
  };
  const auto golden = fixture();
  unsigned accepted = 0, rejected = 0;
  for (unsigned n = 0; n < 20000; ++n) {
    auto bytes = golden;
    for (unsigned edits = 1 + next() % 4; edits; --edits) {
      const auto at = next() % bytes.size();
      const auto bit = next() % 8;
      bytes[at] ^= std::uint8_t(1u << bit);
    }
    if (n % 3 == 0) bytes.resize(next() % 512, 0xff);
    if (n % 4 == 0 && bytes.size() >= 256) sign(bytes);
    Memory memory; sf::Generation runtime(memory.arena());
    const auto result = runtime.begin(bytes.data(), bytes.size(), contract, 1);
    if (result.error != sf::Error::ok) {
      ++rejected;
      memory.unchanged(); zero_result(result); TEST_ASSERT_FALSE(runtime.active());
      TEST_ASSERT_EQUAL_UINT32(0, runtime.last_generation());
    } else {
      ++accepted;
      TEST_ASSERT_TRUE(runtime.active());
      TEST_ASSERT_TRUE(result.image.data_bytes <= 32);
      if (result.image.data_bytes)
        TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data() + result.image.data_load_offset,
                                     memory.arena().data, result.image.data_bytes);
      for (unsigned i = result.image.data_bytes; i < 32; ++i)
        TEST_ASSERT_EQUAL_HEX8(0, memory.arena().data[i]);
    }
    memory.guards();
  }
  std::printf("FMS2 accepted=%u rejected=%u\n", accepted, rejected);
  TEST_ASSERT_TRUE(accepted > 0 && rejected > 0);
}
#endif

int main() {
  UNITY_BEGIN();
#if FIRMINGO_STATEFUL_FUZZ
  RUN_TEST(bounded_stateful_mutations_preserve_guards_and_failed_state);
#else
  RUN_TEST(independent_golden_validates_and_old_format_still_rejects_it);
  RUN_TEST(initialization_copies_data_zeroes_bss_and_scrubs_unused_arena);
  RUN_TEST(active_generation_retains_loop_state_and_requires_explicit_stop);
  RUN_TEST(smaller_replacement_and_failed_start_do_not_leak_or_reinitialize_old_state);
  RUN_TEST(all_truncations_trailing_bytes_and_null_input_reject_without_ram_writes);
  RUN_TEST(metadata_bounds_reject_even_when_hashes_are_recomputed);
  RUN_TEST(header_and_payload_integrity_cover_entries_ram_and_initial_values);
  RUN_TEST(contract_and_actual_arena_must_match_before_initialization);
  RUN_TEST(boundary_layouts_allow_zero_sections_and_the_exact_capsule_and_ram_limits);
  RUN_TEST(aliasing_image_and_ram_and_generation_wrap_are_rejected);
#endif
  return UNITY_END();
}
