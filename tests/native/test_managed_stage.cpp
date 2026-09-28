#include "stage.h"
#include "install.h"
#include "unity.h"

#include <cstring>
#include <vector>

using namespace firmingo_managed;
void setUp() {}
void tearDown() {}

namespace {
constexpr std::uint32_t api = 0x1002154c;
void put16(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t value) {
  bytes[at] = std::uint8_t(value);
  bytes[at + 1] = std::uint8_t(value >> 8);
}
void put32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) bytes[at + i] = std::uint8_t(value >> (8 * i));
}
std::vector<std::uint8_t> image() {
  std::vector<std::uint8_t> bytes(kCodeOffset + 8);
  std::memcpy(bytes.data(), "FMS1", 4);
  put16(bytes, 4, kAbiVersion);
  put16(bytes, 6, kHeaderBytes);
  put32(bytes, 8, kNanoBoardTag);
  put32(bytes, 12, api);
  put32(bytes, 16, kSlotAddress + kCodeOffset);
  put32(bytes, 20, 8);
  put32(bytes, 24, kSlotAddress + kCodeOffset + 1);
  put32(bytes, 28, kSlotAddress + kCodeOffset + 5);
  for (unsigned i = 0; i < 8; ++i) bytes[kCodeOffset + i] = std::uint8_t(i * 19);
  sha256(bytes.data() + kCodeOffset, 8, bytes.data() + 32);
  return bytes;
}
void digest(const std::vector<std::uint8_t>& bytes, std::uint8_t result[32]) {
  sha256(bytes.data(), bytes.size(), result);
}
void expect(StageResult expected, StageResult actual) {
  TEST_ASSERT_EQUAL_INT(int(expected), int(actual));
}
void ready_stage(CapsuleStage& stage, const std::vector<std::uint8_t>& bytes) {
  std::uint8_t expected[32]; digest(bytes, expected);
  expect(StageResult::ok, stage.begin(7, true, bytes.size(), expected, api));
  expect(StageResult::ok, stage.append(7, 0, bytes.data(), bytes.size()));
  expect(StageResult::ok, stage.finish(7));
}
class FakeFlash final : public ProofFlashPort {
 public:
  std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(2 * kProofSectorBytes, 0xa5);
  bool fail_begin = false, fail_erase = false, fail_program = false, bad_readback = false;
  bool open = false;
  unsigned erases = 0, programs = 0, ends = 0;
  bool begin() override { open = !fail_begin; return open; }
  void end() override { TEST_ASSERT_TRUE(open); open = false; ++ends; }
  bool erase(std::uint32_t offset, std::size_t size) override {
    TEST_ASSERT_TRUE(open);
    TEST_ASSERT_EQUAL_HEX32(kSlotAddress - kFlashXipAddress, offset);
    TEST_ASSERT_EQUAL_UINT(kProofSectorBytes, size);
    ++erases;
    if (fail_erase) return false;
    std::memset(bytes.data(), 0xff, size);
    return true;
  }
  bool program(std::uint32_t offset, const std::uint8_t* page,
               std::size_t size) override {
    TEST_ASSERT_TRUE(open);
    TEST_ASSERT_TRUE(page != nullptr);
    TEST_ASSERT_EQUAL_UINT(kProofPageBytes, size);
    TEST_ASSERT_EQUAL_UINT(0, (offset - (kSlotAddress - kFlashXipAddress)) % size);
    const std::size_t at = offset - (kSlotAddress - kFlashXipAddress);
    TEST_ASSERT_TRUE(at + size <= kProofSectorBytes);
    ++programs;
    if (fail_program) return false;
    for (std::size_t i = 0; i < size; ++i) bytes[at + i] &= page[i];
    return true;
  }
  const std::uint8_t* slot_data() const override {
    return bad_readback ? nullptr : bytes.data();
  }
};
}  // namespace

void every_two_part_split_preserves_a_valid_capsule() {
  const auto bytes = image();
  std::uint8_t expected[32]; digest(bytes, expected);
  for (std::size_t split = 1; split < bytes.size(); ++split) {
    CapsuleStage stage;
    expect(StageResult::ok, stage.begin(7, true, bytes.size(), expected, api));
    expect(StageResult::ok, stage.append(7, 0, bytes.data(), split));
    expect(StageResult::ok, stage.append(7, split, bytes.data() + split,
                                         bytes.size() - split));
    expect(StageResult::ok, stage.finish(7));
    std::size_t actual_size = 0;
    const auto* actual = stage.verified(7, &actual_size);
    TEST_ASSERT_NOT_NULL(actual);
    TEST_ASSERT_EQUAL_UINT(bytes.size(), actual_size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data(), actual, actual_size);
  }
}

void unauthorized_and_oversized_begin_have_no_side_effects() {
  CapsuleStage stage;
  std::uint8_t expected[32]{};
  expect(StageResult::unauthorized, stage.begin(7, false, 264, expected, api));
  expect(StageResult::invalid_argument, stage.begin(7, true, kProofStageBytes + 1,
                                                    expected, api));
  expect(StageResult::invalid_argument, stage.begin(0, true, 264, expected, api));
  TEST_ASSERT_FALSE(stage.active());
  expect(StageResult::ok, stage.begin(7, true, 264, expected, api));
  expect(StageResult::busy, stage.begin(8, true, 264, expected, api));
  expect(StageResult::wrong_owner, stage.abort(8));
  TEST_ASSERT_TRUE(stage.active());
  expect(StageResult::ok, stage.abort(7));
  TEST_ASSERT_FALSE(stage.active());
}

void duplicate_overflow_short_and_wrong_owner_chunks_are_rejected() {
  const auto bytes = image();
  std::uint8_t expected[32]; digest(bytes, expected);
  CapsuleStage stage;
  expect(StageResult::ok, stage.begin(7, true, bytes.size(), expected, api));
  expect(StageResult::wrong_owner, stage.append(8, 0, bytes.data(), 1));
  expect(StageResult::ok, stage.append(7, 0, bytes.data(), 1));
  expect(StageResult::wrong_offset, stage.append(7, 0, bytes.data(), 1));
  expect(StageResult::invalid_argument,
         stage.append(7, 1, bytes.data() + 1, bytes.size()));
  expect(StageResult::incomplete, stage.finish(7));
  TEST_ASSERT_EQUAL_UINT(1, stage.received());
  expect(StageResult::ok, stage.abort(7));
}

void transfer_digest_and_image_header_both_must_validate() {
  auto bytes = image();
  std::uint8_t expected[32]; digest(bytes, expected);
  CapsuleStage stage;
  expect(StageResult::ok, stage.begin(7, true, bytes.size(), expected, api));
  bytes.back() ^= 1;
  expect(StageResult::ok, stage.append(7, 0, bytes.data(), bytes.size()));
  expect(StageResult::digest_mismatch, stage.finish(7));
  TEST_ASSERT_FALSE(stage.active());
  bytes = image();
  put32(bytes, 8, 0);  // Wrong board, but the transfer digest itself is correct.
  digest(bytes, expected);
  expect(StageResult::ok, stage.begin(7, true, bytes.size(), expected, api));
  expect(StageResult::ok, stage.append(7, 0, bytes.data(), bytes.size()));
  expect(StageResult::invalid_image, stage.finish(7));
  TEST_ASSERT_FALSE(stage.active());
}

void trailing_capsule_bytes_are_rejected_before_install() {
  auto bytes = image();
  bytes.push_back(0);
  std::uint8_t expected[32]; digest(bytes, expected);
  CapsuleStage stage;
  expect(StageResult::ok, stage.begin(7, true, bytes.size(), expected, api));
  expect(StageResult::ok, stage.append(7, 0, bytes.data(), bytes.size()));
  expect(StageResult::invalid_image, stage.finish(7));
  FakeFlash flash;
  TEST_ASSERT_EQUAL_INT(int(InstallResult::no_verified_capsule),
                        int(install_verified_capsule(stage, 7, api, flash).result));
  TEST_ASSERT_EQUAL_UINT(0, flash.erases);
}

void installer_writes_only_first_slot_sector_and_pads_last_page() {
  const auto bytes = image();
  CapsuleStage stage; ready_stage(stage, bytes);
  FakeFlash flash;
  const auto report = install_verified_capsule(stage, 7, api, flash);
  TEST_ASSERT_EQUAL_INT(int(InstallResult::ok), int(report.result));
  TEST_ASSERT_TRUE(report.slot_touched);
  TEST_ASSERT_EQUAL_UINT(1, flash.erases);
  TEST_ASSERT_EQUAL_UINT(2, flash.programs);
  TEST_ASSERT_EQUAL_UINT(1, flash.ends);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data(), flash.bytes.data(), bytes.size());
  for (std::size_t i = bytes.size(); i < kProofSectorBytes; ++i)
    TEST_ASSERT_EQUAL_HEX8(0xff, flash.bytes[i]);
  for (std::size_t i = kProofSectorBytes; i < flash.bytes.size(); ++i)
    TEST_ASSERT_EQUAL_HEX8(0xa5, flash.bytes[i]);
}

void installer_reports_prewrite_and_partial_write_failures() {
  const auto bytes = image();
  CapsuleStage stage; ready_stage(stage, bytes);
  FakeFlash flash;
  auto report = install_verified_capsule(stage, 8, api, flash);
  TEST_ASSERT_EQUAL_INT(int(InstallResult::no_verified_capsule), int(report.result));
  TEST_ASSERT_FALSE(report.slot_touched);
  flash.fail_begin = true;
  report = install_verified_capsule(stage, 7, api, flash);
  TEST_ASSERT_EQUAL_INT(int(InstallResult::flash_begin_failed), int(report.result));
  TEST_ASSERT_FALSE(report.slot_touched);
  TEST_ASSERT_EQUAL_UINT(0, flash.erases);
  flash.fail_begin = false;
  flash.fail_erase = true;
  report = install_verified_capsule(stage, 7, api, flash);
  TEST_ASSERT_EQUAL_INT(int(InstallResult::erase_failed), int(report.result));
  TEST_ASSERT_TRUE(report.slot_touched);
  TEST_ASSERT_EQUAL_UINT(1, flash.ends);
  flash.fail_erase = false;
  flash.fail_program = true;
  report = install_verified_capsule(stage, 7, api, flash);
  TEST_ASSERT_EQUAL_INT(int(InstallResult::program_failed), int(report.result));
  TEST_ASSERT_TRUE(report.slot_touched);
  TEST_ASSERT_EQUAL_UINT(2, flash.ends);
  flash.fail_program = false;
  flash.bad_readback = true;
  report = install_verified_capsule(stage, 7, api, flash);
  TEST_ASSERT_EQUAL_INT(int(InstallResult::readback_failed), int(report.result));
  TEST_ASSERT_TRUE(report.slot_touched);
  TEST_ASSERT_EQUAL_UINT(3, flash.ends);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(every_two_part_split_preserves_a_valid_capsule);
  RUN_TEST(unauthorized_and_oversized_begin_have_no_side_effects);
  RUN_TEST(duplicate_overflow_short_and_wrong_owner_chunks_are_rejected);
  RUN_TEST(transfer_digest_and_image_header_both_must_validate);
  RUN_TEST(trailing_capsule_bytes_are_rejected_before_install);
  RUN_TEST(installer_writes_only_first_slot_sector_and_pads_last_page);
  RUN_TEST(installer_reports_prewrite_and_partial_write_failures);
  return UNITY_END();
}
