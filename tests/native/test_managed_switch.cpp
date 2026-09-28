#include "switch.h"
#include "unity.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

using namespace firmingo_managed;
void setUp() {}
void tearDown() {}

namespace {
constexpr std::uint32_t api = 0x1002154c;
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
  for (unsigned i = 0; i < 8; ++i) bytes[kCodeOffset + i] = i * 13;
  sha256(bytes.data() + kCodeOffset, 8, bytes.data() + 32);
  return bytes;
}
void stage_capsule(CapsuleStage& stage, const std::vector<std::uint8_t>& bytes) {
  std::uint8_t digest[32]; sha256(bytes.data(), bytes.size(), digest);
  TEST_ASSERT_EQUAL_INT(int(StageResult::ok),
      int(stage.begin(7, true, bytes.size(), digest, api)));
  TEST_ASSERT_EQUAL_INT(int(StageResult::ok),
      int(stage.append(7, 0, bytes.data(), bytes.size())));
  TEST_ASSERT_EQUAL_INT(int(StageResult::ok), int(stage.finish(7)));
}
struct FakeFlash : ProofFlashPort {
  std::uint8_t sector[kProofSectorBytes]{};
  bool fail_program = false, open = false;
  unsigned erases = 0;
  bool begin() override { open = true; return true; }
  void end() override { open = false; }
  bool erase(std::uint32_t offset, std::size_t size) override {
    TEST_ASSERT_TRUE(open);
    TEST_ASSERT_EQUAL_HEX32(kSlotAddress - kFlashXipAddress, offset);
    TEST_ASSERT_EQUAL_UINT(kProofSectorBytes, size);
    ++erases;
    std::memset(sector, 0xff, sizeof(sector));
    return true;
  }
  bool program(std::uint32_t offset, const std::uint8_t* page,
               std::size_t size) override {
    TEST_ASSERT_TRUE(open);
    TEST_ASSERT_EQUAL_UINT(kProofPageBytes, size);
    if (fail_program) return false;
    const std::size_t at = offset - (kSlotAddress - kFlashXipAddress);
    TEST_ASSERT_TRUE(at + size <= sizeof(sector));
    std::memcpy(sector + at, page, size);
    return true;
  }
  const std::uint8_t* slot_data() const override { return sector; }
};
struct FakeRuntime : SketchRuntime {
  unsigned stops = 0, starts = 0;
  ImageInfo image{};
  void stop() override { ++stops; }
  void start(const ImageInfo& next) override { image = next; ++starts; }
};
bool await_park(Core1Pause& pause) {
  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!pause.parked() && std::chrono::steady_clock::now() < until)
    std::this_thread::yield();
  return pause.parked();
}
}  // namespace

void missing_capsule_and_stalled_sketch_never_touch_flash() {
  CapsuleStage stage;
  Core1Pause pause;
  FakeFlash flash;
  FakeRuntime runtime;
  SketchSwitch switching(stage, pause, flash, runtime);
  TEST_ASSERT_FALSE(switching.begin(7, api, 0));
  const auto bytes = capsule(); stage_capsule(stage, bytes);
  TEST_ASSERT_FALSE(switching.begin(8, api, 0));
  TEST_ASSERT_TRUE(switching.begin(7, api, UINT32_MAX - 9));
  TEST_ASSERT_EQUAL_INT(int(SwitchStatus::waiting), int(switching.poll(500, true).status));
  const auto report = switching.poll(1000, true);
  TEST_ASSERT_EQUAL_INT(int(SwitchStatus::timeout), int(report.status));
  TEST_ASSERT_FALSE(report.slot_touched);
  TEST_ASSERT_EQUAL_UINT(0, flash.erases);
  TEST_ASSERT_EQUAL_UINT(0, runtime.stops);
  TEST_ASSERT_FALSE(stage.active());
}

void parked_core_switches_to_new_image_before_release() {
  CapsuleStage stage;
  Core1Pause pause;
  FakeFlash flash;
  FakeRuntime runtime;
  SketchSwitch switching(stage, pause, flash, runtime);
  const auto bytes = capsule(); stage_capsule(stage, bytes);
  TEST_ASSERT_TRUE(switching.begin(7, api, 100));
  std::thread core1([&] { pause.park_if_requested(); });
  const bool parked = await_park(pause);
  TEST_ASSERT_TRUE(parked);
  const auto report = switching.poll(101, true);
  core1.join();
  TEST_ASSERT_EQUAL_INT(int(SwitchStatus::complete), int(report.status));
  TEST_ASSERT_TRUE(report.slot_touched);
  TEST_ASSERT_EQUAL_UINT(1, runtime.stops);
  TEST_ASSERT_EQUAL_UINT(1, runtime.starts);
  TEST_ASSERT_EQUAL_HEX32(kSlotAddress + kCodeOffset + 1, runtime.image.setup_address);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data(), flash.sector, bytes.size());
  TEST_ASSERT_FALSE(stage.active());
  TEST_ASSERT_FALSE(switching.active());
}

void partial_program_failure_stops_old_sketch_and_does_not_start_new_one() {
  CapsuleStage stage;
  Core1Pause pause;
  FakeFlash flash;
  FakeRuntime runtime;
  flash.fail_program = true;
  SketchSwitch switching(stage, pause, flash, runtime);
  const auto bytes = capsule(); stage_capsule(stage, bytes);
  TEST_ASSERT_TRUE(switching.begin(7, api, 100));
  std::thread core1([&] { pause.park_if_requested(); });
  const bool parked = await_park(pause);
  TEST_ASSERT_TRUE(parked);
  const auto report = switching.poll(101, true);
  core1.join();
  TEST_ASSERT_EQUAL_INT(int(SwitchStatus::install_failed), int(report.status));
  TEST_ASSERT_EQUAL_INT(int(InstallResult::program_failed), int(report.install));
  TEST_ASSERT_TRUE(report.slot_touched);
  TEST_ASSERT_EQUAL_UINT(1, runtime.stops);
  TEST_ASSERT_EQUAL_UINT(0, runtime.starts);
}

void arm_removed_after_staging_cancels_without_flash() {
  CapsuleStage stage;
  Core1Pause pause;
  FakeFlash flash;
  FakeRuntime runtime;
  SketchSwitch switching(stage, pause, flash, runtime);
  const auto bytes = capsule(); stage_capsule(stage, bytes);
  TEST_ASSERT_TRUE(switching.begin(7, api, 100));
  const auto report = switching.poll(101, false);
  TEST_ASSERT_EQUAL_INT(int(SwitchStatus::denied), int(report.status));
  TEST_ASSERT_FALSE(report.slot_touched);
  TEST_ASSERT_EQUAL_UINT(0, flash.erases);
  TEST_ASSERT_EQUAL_UINT(0, runtime.stops);
  TEST_ASSERT_FALSE(stage.active());
}

void arm_removed_after_core_park_cancels_without_flash() {
  CapsuleStage stage;
  Core1Pause pause;
  FakeFlash flash;
  FakeRuntime runtime;
  SketchSwitch switching(stage, pause, flash, runtime);
  const auto bytes = capsule(); stage_capsule(stage, bytes);
  TEST_ASSERT_TRUE(switching.begin(7, api, 100));
  std::thread core1([&] { pause.park_if_requested(); });
  const bool parked = await_park(pause);
  if (!parked) {
    pause.release(); core1.join();
    TEST_FAIL_MESSAGE("core 1 did not reach the park point");
  }
  const auto report = switching.poll(101, false);
  core1.join();
  TEST_ASSERT_EQUAL_INT(int(SwitchStatus::denied), int(report.status));
  TEST_ASSERT_FALSE(report.slot_touched);
  TEST_ASSERT_EQUAL_UINT(0, flash.erases);
  TEST_ASSERT_EQUAL_UINT(0, runtime.stops);
  TEST_ASSERT_FALSE(stage.active());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(missing_capsule_and_stalled_sketch_never_touch_flash);
  RUN_TEST(parked_core_switches_to_new_image_before_release);
  RUN_TEST(partial_program_failure_stops_old_sketch_and_does_not_start_new_one);
  RUN_TEST(arm_removed_after_staging_cancels_without_flash);
  RUN_TEST(arm_removed_after_core_park_cancels_without_flash);
  return UNITY_END();
}
