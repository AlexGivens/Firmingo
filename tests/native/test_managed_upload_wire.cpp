#include "upload_wire.h"
#include "unity.h"

#include <cstring>
#include <vector>

using namespace firmingo_managed;
void setUp() {}
void tearDown() {}

namespace {
constexpr std::uint32_t api = 0x1002154c;
constexpr char id[] = "5031503337360009";
std::vector<std::uint8_t> capsule() {
  std::vector<std::uint8_t> bytes(kCodeOffset + 8);
  std::memcpy(bytes.data(), "FMS1", 4);
  bytes[4] = kAbiVersion;
  bytes[6] = kHeaderBytes;
  const auto put32 = [&](std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[at + i] = value >> (8 * i);
  };
  put32(8, kNanoBoardTag);
  put32(12, api);
  put32(16, kSlotAddress + kCodeOffset);
  put32(20, 8);
  put32(24, kSlotAddress + kCodeOffset + 1);
  put32(28, kSlotAddress + kCodeOffset + 5);
  for (unsigned i = 0; i < 8; ++i) bytes[kCodeOffset + i] = i * 17;
  sha256(bytes.data() + kCodeOffset, 8, bytes.data() + 32);
  return bytes;
}
std::vector<std::uint8_t> message() {
  const auto image = capsule();
  std::vector<std::uint8_t> bytes(kUploadHeaderBytes + image.size() + 4);
  std::memcpy(bytes.data(), "FMSU", 4);
  bytes[4] = 1;
  const auto size = image.size();
  for (unsigned i = 0; i < 4; ++i) bytes[8 + i] = size >> (8 * i);
  sha256(image.data(), image.size(), bytes.data() + 12);
  std::memcpy(bytes.data() + 44, id, 16);
  std::memcpy(bytes.data() + kUploadHeaderBytes, image.data(), image.size());
  std::memcpy(bytes.data() + kUploadHeaderBytes + image.size(), "GO!!", 4);
  return bytes;
}
}  // namespace

void every_two_part_split_of_valid_upload_reaches_ready() {
  const auto bytes = message();
  for (std::size_t split = 1; split < bytes.size(); ++split) {
    CapsuleStage stage;
    UploadReceiver receiver(stage, 7, api, id);
    TEST_ASSERT_EQUAL_INT(int(UploadStatus::receiving),
                          int(receiver.feed(bytes.data(), split, true)));
    TEST_ASSERT_EQUAL_INT(int(UploadStatus::ready),
                          int(receiver.feed(bytes.data() + split,
                                            bytes.size() - split, true)));
    std::size_t size = 0;
    TEST_ASSERT_NOT_NULL(stage.verified(7, &size));
    TEST_ASSERT_EQUAL_UINT(capsule().size(), size);
  }
}

void malformed_header_wrong_board_and_unarmed_upload_never_verify() {
  auto bytes = message();
  for (std::size_t at : {std::size_t(0), std::size_t(4), std::size_t(5),
                         std::size_t(8), std::size_t(44)}) {
    auto altered = bytes;
    altered[at] ^= 1;
    CapsuleStage stage;
    UploadReceiver receiver(stage, 7, api, id);
    TEST_ASSERT_EQUAL_INT(int(UploadStatus::invalid),
                          int(receiver.feed(altered.data(), altered.size(), true)));
    TEST_ASSERT_FALSE(stage.active());
  }
  CapsuleStage stage;
  UploadReceiver receiver(stage, 7, api, id);
  TEST_ASSERT_EQUAL_INT(int(UploadStatus::denied),
                        int(receiver.feed(bytes.data(), bytes.size(), false)));
  TEST_ASSERT_FALSE(stage.active());
}

void corruption_extra_bytes_and_lost_arm_reject_before_commit() {
  auto bytes = message();
  bytes[kUploadHeaderBytes + kCodeOffset] ^= 1;
  CapsuleStage corrupt_stage;
  UploadReceiver corrupt(corrupt_stage, 7, api, id);
  TEST_ASSERT_EQUAL_INT(int(UploadStatus::invalid),
                        int(corrupt.feed(bytes.data(), bytes.size(), true)));
  TEST_ASSERT_FALSE(corrupt_stage.active());

  bytes = message();
  bytes.push_back(0);
  CapsuleStage extra_stage;
  UploadReceiver extra(extra_stage, 7, api, id);
  TEST_ASSERT_EQUAL_INT(int(UploadStatus::invalid),
                        int(extra.feed(bytes.data(), bytes.size(), true)));
  TEST_ASSERT_FALSE(extra_stage.active());

  bytes = message();
  CapsuleStage disarmed_stage;
  UploadReceiver disarmed(disarmed_stage, 7, api, id);
  TEST_ASSERT_EQUAL_INT(int(UploadStatus::receiving),
      int(disarmed.feed(bytes.data(), bytes.size() - 4, true)));
  TEST_ASSERT_EQUAL_INT(int(UploadStatus::denied),
      int(disarmed.feed(bytes.data() + bytes.size() - 4, 4, false)));
  TEST_ASSERT_FALSE(disarmed_stage.active());
}

void disconnect_aborts_staged_bytes() {
  const auto bytes = message();
  CapsuleStage stage;
  UploadReceiver receiver(stage, 7, api, id);
  TEST_ASSERT_EQUAL_INT(int(UploadStatus::receiving),
      int(receiver.feed(bytes.data(), bytes.size() - 4, true)));
  TEST_ASSERT_TRUE(stage.active());
  receiver.abort();
  TEST_ASSERT_FALSE(stage.active());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(every_two_part_split_of_valid_upload_reaches_ready);
  RUN_TEST(malformed_header_wrong_board_and_unarmed_upload_never_verify);
  RUN_TEST(corruption_extra_bytes_and_lost_arm_reject_before_commit);
  RUN_TEST(disconnect_aborts_staged_bytes);
  return UNITY_END();
}
