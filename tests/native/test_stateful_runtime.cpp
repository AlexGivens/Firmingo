#include "stateful_runtime.h"
#include "unity.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <thread>
#include <vector>

namespace fm = firmingo_managed;
namespace sf = fm::stateful;
void setUp() {}
void tearDown() {}

namespace {
constexpr sf::Contract contract{0x10000100, 0x20030000, 32, 4096};
std::vector<std::uint8_t> fixture() {
  std::ifstream input(STATEFUL_FIXTURE_DIR "/initialized.hex");
  TEST_ASSERT_TRUE(input.good());
  std::vector<std::uint8_t> bytes;
  unsigned value;
  while (input >> std::hex >> value) bytes.push_back(static_cast<std::uint8_t>(value));
  TEST_ASSERT_EQUAL_UINT(272, bytes.size());
  return bytes;
}
void put32(std::vector<std::uint8_t>& bytes, unsigned offset, std::uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = value >> (8 * i);
}
void sign(std::vector<std::uint8_t>& bytes) {
  fm::sha256(bytes.data() + 256, bytes.size() - 256, bytes.data() + 64);
  fm::sha256(bytes.data(), 96, bytes.data() + 96);
}
struct Memory {
  std::array<std::uint8_t, 48> bytes;
  Memory() { bytes.fill(0xa5); }
  std::uint8_t* ram() { return bytes.data() + 8; }
  sf::Arena arena() { return {ram(), 32, contract.state_address}; }
  void guards() const {
    for (unsigned i = 0; i < 8; ++i) {
      TEST_ASSERT_EQUAL_HEX8(0xa5, bytes[i]);
      TEST_ASSERT_EQUAL_HEX8(0xa5, bytes[40 + i]);
    }
  }
};
struct Entries : sf::ExecutionPort {
  Memory& memory;
  unsigned setups = 0, loops = 0, stops = 0;
  bool initialized_before_setup = true;
  std::size_t data_bytes = 8;
  explicit Entries(Memory& m) : memory(m) {}
  void stopped() override { ++stops; }
  void invoke(std::uint32_t entry) override {
    if (entry == 0x10200101) {
      ++setups;
      initialized_before_setup &= memory.ram()[0] == 0x44 &&
          std::all_of(memory.ram() + data_bytes, memory.ram() + 32,
                      [](std::uint8_t b) { return b == 0; });
      memory.ram()[0] = 42;  // Model user setup's first write after initialization.
    } else if (entry == 0x10200105) {
      ++loops;
      ++memory.ram()[data_bytes];
    } else {
      initialized_before_setup = false;
    }
  }
};
enum class Fault { none, begin, erase, program, capsule_readback, tail_readback, missing_readback };
struct Flash : fm::ProofFlashPort {
  std::array<std::uint8_t, 4096> bytes;
  sf::Runtime& runtime;
  fm::Core1Pause& pause;
  Fault fault = Fault::none;
  unsigned begins = 0, ends = 0, erases = 0, pages = 0;
  bool order_ok = true, open = false;
  Flash(sf::Runtime& r, fm::Core1Pause& p) : runtime(r), pause(p) { bytes.fill(0xff); }
  bool begin() override {
    ++begins;
    order_ok &= pause.parked() && runtime.active_generation() == 0;
    open = fault != Fault::begin;
    return open;
  }
  void end() override {
    ++ends;
    order_ok &= open && pause.parked() && runtime.active_generation() == 0;
    if (fault == Fault::capsule_readback) bytes[256] ^= 1;
    if (fault == Fault::tail_readback) bytes[4095] = 0;
    open = false;
  }
  bool erase(std::uint32_t offset, std::size_t count) override {
    ++erases;
    order_ok &= open && pause.parked() && offset == fm::kSlotAddress - fm::kFlashXipAddress && count == 4096;
    if (fault == Fault::erase) { bytes[0] = 0; return false; }
    bytes.fill(0xff);
    return true;
  }
  bool program(std::uint32_t offset, const std::uint8_t* page, std::size_t count) override {
    ++pages;
    const auto at = offset - (fm::kSlotAddress - fm::kFlashXipAddress);
    order_ok &= open && pause.parked() && count == 256 && at % 256 == 0 && at <= 4096 - 256;
    if (!order_ok) return false;
    // A failure after one complete page leaves a genuinely partial capsule.
    if (fault == Fault::program && pages == 2) return false;
    std::memcpy(bytes.data() + at, page, count);
    return true;
  }
  const std::uint8_t* slot_data() const override {
    return fault == Fault::missing_readback ? nullptr : bytes.data();
  }
};
sf::ReplacementReport parked_poll(sf::Replacement& replacement, fm::Core1Pause& pause,
                                   sf::Runtime& runtime, bool armed = true) {
  std::thread core1([&] { pause.park_if_requested(); runtime.tick(); });
  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!pause.parked() && std::chrono::steady_clock::now() < until) std::this_thread::yield();
  const bool parked = pause.parked();
  sf::ReplacementReport report{};
  if (parked) report = replacement.poll(101, armed);
  else pause.release();
  core1.join();
  TEST_ASSERT_TRUE_MESSAGE(parked, "core 1 did not reach its production SRAM park boundary");
  return report;
}
}  // namespace

void boot_derives_exact_span_and_initializes_before_one_setup_and_repeated_loops() {
  auto slot = fixture(); slot.resize(4096, 0xff);
  Memory m; Entries e(m); sf::Runtime runtime(m.arena(), contract, e);
  TEST_ASSERT_FALSE(runtime.tick());
  TEST_ASSERT_EQUAL_INT(int(sf::Error::ok), int(runtime.boot_from_slot(slot.data(), slot.size()).error));
  TEST_ASSERT_EQUAL_UINT(1, runtime.active_generation());
  TEST_ASSERT_FALSE(runtime.loop_returned());
  TEST_ASSERT_EQUAL_UINT(0, e.setups);
  TEST_ASSERT_TRUE(runtime.tick());
  TEST_ASSERT_TRUE(runtime.tick());
  TEST_ASSERT_TRUE(e.initialized_before_setup);
  TEST_ASSERT_EQUAL_UINT(1, e.setups);
  TEST_ASSERT_EQUAL_UINT(2, e.loops);
  TEST_ASSERT_EQUAL_UINT(2, m.ram()[8]);
  TEST_ASSERT_TRUE(runtime.loop_returned());
  TEST_ASSERT_EQUAL_INT(int(sf::Error::bad_generation), int(runtime.boot_from_slot(slot.data(), slot.size()).error));
  TEST_ASSERT_EQUAL_UINT(2, m.ram()[8]);
  m.guards();
}

void truncated_or_corrupt_mapped_slots_never_publish_or_write_ram() {
  const auto valid = fixture();
  for (std::size_t count = 0; count < valid.size(); ++count) {
    const std::vector<std::uint8_t> short_slot(valid.begin(), valid.begin() + count);
    Memory m; Entries e(m); sf::Runtime runtime(m.arena(), contract, e);
    TEST_ASSERT_NOT_EQUAL(int(sf::Error::ok), int(runtime.boot_from_slot(short_slot.data(), count).error));
    TEST_ASSERT_FALSE(runtime.active_generation());
    TEST_ASSERT_FALSE(runtime.tick());
    for (auto b : m.bytes) TEST_ASSERT_EQUAL_HEX8(0xa5, b);
  }
  for (const std::uint32_t length : {0u, 255u, 256u, 4097u, UINT32_MAX}) {
    auto slot = valid; put32(slot, 52, length);
    Memory m; Entries e(m); sf::Runtime runtime(m.arena(), contract, e);
    TEST_ASSERT_NOT_EQUAL(int(sf::Error::ok), int(runtime.boot_from_slot(slot.data(), slot.size()).error));
    TEST_ASSERT_FALSE(runtime.tick());
    for (auto b : m.bytes) TEST_ASSERT_EQUAL_HEX8(0xa5, b);
  }
}

void invalid_unarmed_busy_and_timed_out_requests_preserve_the_running_generation() {
  const auto image = fixture();
  Memory m; Entries e(m); sf::Runtime runtime(m.arena(), contract, e);
  fm::Core1Pause pause; Flash flash(runtime, pause); sf::Replacement next(runtime, pause, flash);
  TEST_ASSERT_FALSE(next.request(image.data(), image.size(), true, 0));
  runtime.boot_from_slot(image.data(), image.size()); runtime.tick();
  TEST_ASSERT_FALSE(next.request(image.data(), image.size(), false, 0));
  auto bad = image; bad[0] ^= 1;
  TEST_ASSERT_FALSE(next.request(bad.data(), bad.size(), true, 0));
  TEST_ASSERT_TRUE(pause.request());
  TEST_ASSERT_FALSE(next.request(image.data(), image.size(), true, 0));
  pause.release();
  TEST_ASSERT_TRUE(next.request(image.data(), image.size(), true, UINT32_MAX - 9));
  TEST_ASSERT_FALSE(next.request(image.data(), image.size(), true, 0));
  TEST_ASSERT_EQUAL_INT(int(sf::ReplacementStatus::waiting), int(next.poll(500, true).status));
  TEST_ASSERT_EQUAL_INT(int(sf::ReplacementStatus::timeout), int(next.poll(1000, true).status));
  TEST_ASSERT_EQUAL_UINT(0, flash.begins);
  TEST_ASSERT_EQUAL_UINT(0, e.stops);
  TEST_ASSERT_EQUAL_UINT(1, runtime.active_generation());
  TEST_ASSERT_EQUAL_UINT(1, m.ram()[8]);
  TEST_ASSERT_FALSE(next.active());
  TEST_ASSERT_TRUE(runtime.tick());
  TEST_ASSERT_EQUAL_UINT(2, m.ram()[8]);
}

void replacement_owns_snapshot_stops_before_flash_and_publishes_only_after_initialization() {
  auto image = fixture(); const auto original = image;
  Memory m; Entries e(m); sf::Runtime runtime(m.arena(), contract, e);
  fm::Core1Pause pause; Flash flash(runtime, pause); sf::Replacement next(runtime, pause, flash);
  runtime.boot_from_slot(image.data(), image.size()); runtime.tick();
  TEST_ASSERT_TRUE(next.request(image.data(), image.size(), true, 100));
  image[256] ^= 1;  // Caller mutation after request cannot change the captured operation.
  const auto report = parked_poll(next, pause, runtime);
  TEST_ASSERT_EQUAL_INT(int(sf::ReplacementStatus::complete), int(report.status));
  TEST_ASSERT_TRUE(report.slot_touched);
  TEST_ASSERT_TRUE(flash.order_ok);
  TEST_ASSERT_EQUAL_UINT(1, e.stops);
  TEST_ASSERT_EQUAL_UINT(2, e.setups);
  TEST_ASSERT_EQUAL_UINT(2, e.loops);
  TEST_ASSERT_TRUE(e.initialized_before_setup);
  TEST_ASSERT_EQUAL_UINT(2, runtime.active_generation());
  TEST_ASSERT_TRUE(runtime.loop_returned());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(original.data(), flash.bytes.data(), original.size());
  for (std::size_t i = original.size(); i < 4096; ++i) TEST_ASSERT_EQUAL_HEX8(0xff, flash.bytes[i]);
  TEST_ASSERT_FALSE(next.active());
  m.guards();
}

void removing_arm_before_or_after_park_never_stops_or_writes() {
  for (bool park : {false, true}) {
    const auto image = fixture();
    Memory m; Entries e(m); sf::Runtime runtime(m.arena(), contract, e);
    fm::Core1Pause pause; Flash flash(runtime, pause); sf::Replacement next(runtime, pause, flash);
    runtime.boot_from_slot(image.data(), image.size()); runtime.tick();
    TEST_ASSERT_TRUE(next.request(image.data(), image.size(), true, 100));
    const auto report = park ? parked_poll(next, pause, runtime, false) : next.poll(101, false);
    TEST_ASSERT_EQUAL_INT(int(sf::ReplacementStatus::denied), int(report.status));
    TEST_ASSERT_FALSE(report.slot_touched);
    TEST_ASSERT_EQUAL_UINT(0, flash.begins);
    TEST_ASSERT_EQUAL_UINT(0, e.stops);
    TEST_ASSERT_EQUAL_UINT(1, e.setups);
    TEST_ASSERT_EQUAL_UINT(1, runtime.active_generation());
  }
}

void every_flash_failure_keeps_execution_disabled_after_releasing_core_one() {
  const Fault faults[] = {Fault::begin, Fault::erase, Fault::program, Fault::capsule_readback,
                          Fault::tail_readback, Fault::missing_readback};
  const fm::InstallResult expected[] = {fm::InstallResult::flash_begin_failed, fm::InstallResult::erase_failed,
      fm::InstallResult::program_failed, fm::InstallResult::readback_failed, fm::InstallResult::readback_failed,
      fm::InstallResult::readback_failed};
  for (unsigned i = 0; i < 6; ++i) {
    const auto image = fixture();
    Memory m; Entries e(m); sf::Runtime runtime(m.arena(), contract, e);
    fm::Core1Pause pause; Flash flash(runtime, pause); sf::Replacement next(runtime, pause, flash);
    runtime.boot_from_slot(image.data(), image.size()); runtime.tick();
    flash.fault = faults[i];
    TEST_ASSERT_TRUE(next.request(image.data(), image.size(), true, 100));
    const auto report = parked_poll(next, pause, runtime);
    TEST_ASSERT_EQUAL_INT(int(sf::ReplacementStatus::failed), int(report.status));
    TEST_ASSERT_EQUAL_INT(int(expected[i]), int(report.install));
    TEST_ASSERT_EQUAL_INT(i != 0, report.slot_touched);
    TEST_ASSERT_TRUE(flash.order_ok);
    TEST_ASSERT_EQUAL_UINT(1, e.stops);
    TEST_ASSERT_EQUAL_UINT(1, e.setups);
    TEST_ASSERT_FALSE(runtime.active_generation());
    TEST_ASSERT_FALSE(runtime.loop_returned());
    TEST_ASSERT_FALSE(runtime.tick());
    TEST_ASSERT_EQUAL_INT(int(sf::ReplacementStatus::idle), int(next.poll(102, true).status));
    TEST_ASSERT_FALSE(next.active());
    m.guards();
  }
}

void explicit_recovery_after_partial_write_and_smaller_replacement_scrub_state() {
  auto image = fixture();
  Memory m; Entries e(m); sf::Runtime runtime(m.arena(), contract, e);
  fm::Core1Pause pause; Flash flash(runtime, pause); sf::Replacement next(runtime, pause, flash);
  runtime.boot_from_slot(image.data(), image.size()); runtime.tick();
  flash.fault = Fault::program;
  TEST_ASSERT_TRUE(next.request(image.data(), image.size(), true, 100));
  TEST_ASSERT_EQUAL_INT(int(sf::ReplacementStatus::failed), int(parked_poll(next, pause, runtime).status));
  // Reboot validates the actual partial slot and does not execute its entries.
  Memory reboot_memory; Entries reboot_entries(reboot_memory);
  sf::Runtime reboot(reboot_memory.arena(), contract, reboot_entries);
  TEST_ASSERT_NOT_EQUAL(int(sf::Error::ok), int(reboot.boot_from_slot(flash.bytes.data(), flash.bytes.size()).error));
  TEST_ASSERT_FALSE(reboot.tick());
  image.resize(268); put32(image, 36, 4); put32(image, 40, contract.state_address + 4);
  put32(image, 44, 4); put32(image, 52, image.size()); sign(image);
  flash.fault = Fault::none; e.data_bytes = 4;
  std::fill(m.ram(), m.ram() + 32, 0xcc);
  TEST_ASSERT_TRUE(next.request(image.data(), image.size(), true, 100));
  TEST_ASSERT_EQUAL_INT(int(sf::ReplacementStatus::complete), int(parked_poll(next, pause, runtime).status));
  TEST_ASSERT_TRUE(e.initialized_before_setup);
  TEST_ASSERT_EQUAL_UINT(2, runtime.active_generation());
  for (unsigned at = 5; at < 32; ++at) TEST_ASSERT_EQUAL_UINT(0, m.ram()[at]);
  m.guards();
}

void fresh_runtime_boot_resets_ram_instead_of_persisting_a_previous_run() {
  const auto image = fixture();
  Memory m; Entries e(m);
  { sf::Runtime runtime(m.arena(), contract, e);
    runtime.boot_from_slot(image.data(), image.size()); runtime.tick(); runtime.tick(); }
  Entries fresh_entries(m); sf::Runtime fresh(m.arena(), contract, fresh_entries);
  TEST_ASSERT_EQUAL_INT(int(sf::Error::ok), int(fresh.boot_from_slot(image.data(), image.size()).error));
  TEST_ASSERT_EQUAL_UINT(1, fresh.active_generation());
  TEST_ASSERT_EQUAL_UINT(0, m.ram()[8]);
  fresh.tick();
  TEST_ASSERT_TRUE(fresh_entries.initialized_before_setup);
  TEST_ASSERT_EQUAL_UINT(1, fresh_entries.setups);
  m.guards();
}

void invalid_boot_remains_idle_until_a_separate_explicit_replacement_succeeds() {
  const auto image = fixture();
  Memory m; Entries e(m); sf::Runtime runtime(m.arena(), contract, e);
  fm::Core1Pause pause; Flash flash(runtime, pause); sf::Replacement next(runtime, pause, flash);
  TEST_ASSERT_NOT_EQUAL(int(sf::Error::ok), int(runtime.boot_from_slot(flash.bytes.data(), flash.bytes.size()).error));
  TEST_ASSERT_FALSE(runtime.tick());
  TEST_ASSERT_EQUAL_UINT(0, e.setups);
  TEST_ASSERT_TRUE(next.request(image.data(), image.size(), true, 100));
  TEST_ASSERT_EQUAL_INT(int(sf::ReplacementStatus::complete), int(parked_poll(next, pause, runtime).status));
  TEST_ASSERT_EQUAL_UINT(1, runtime.active_generation());
  TEST_ASSERT_EQUAL_UINT(1, e.setups);
  TEST_ASSERT_TRUE(e.initialized_before_setup);
  m.guards();
}

void initialization_failure_after_good_readback_never_publishes_entries() {
  const auto image = fixture();
  Memory m; Entries e(m);
  auto arena = m.arena(); arena.mapped_address += 4;  // Deliberately incorrect port wiring.
  sf::Runtime runtime(arena, contract, e);
  fm::Core1Pause pause; Flash flash(runtime, pause); sf::Replacement next(runtime, pause, flash);
  TEST_ASSERT_EQUAL_INT(int(sf::Error::wrong_arena), int(runtime.boot_from_slot(image.data(), image.size()).error));
  TEST_ASSERT_TRUE(next.request(image.data(), image.size(), true, 100));
  const auto report = parked_poll(next, pause, runtime);
  TEST_ASSERT_EQUAL_INT(int(sf::ReplacementStatus::failed), int(report.status));
  TEST_ASSERT_EQUAL_INT(int(fm::InstallResult::ok), int(report.install));
  TEST_ASSERT_EQUAL_INT(int(sf::Error::wrong_arena), int(report.image_error));
  TEST_ASSERT_TRUE(report.slot_touched);
  TEST_ASSERT_FALSE(runtime.active_generation());
  TEST_ASSERT_EQUAL_UINT(0, e.setups);
  for (auto b : m.bytes) TEST_ASSERT_EQUAL_HEX8(0xa5, b);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(boot_derives_exact_span_and_initializes_before_one_setup_and_repeated_loops);
  RUN_TEST(truncated_or_corrupt_mapped_slots_never_publish_or_write_ram);
  RUN_TEST(invalid_unarmed_busy_and_timed_out_requests_preserve_the_running_generation);
  RUN_TEST(replacement_owns_snapshot_stops_before_flash_and_publishes_only_after_initialization);
  RUN_TEST(removing_arm_before_or_after_park_never_stops_or_writes);
  RUN_TEST(every_flash_failure_keeps_execution_disabled_after_releasing_core_one);
  RUN_TEST(explicit_recovery_after_partial_write_and_smaller_replacement_scrub_state);
  RUN_TEST(fresh_runtime_boot_resets_ram_instead_of_persisting_a_previous_run);
  RUN_TEST(invalid_boot_remains_idle_until_a_separate_explicit_replacement_succeeds);
  RUN_TEST(initialization_failure_after_good_readback_never_publishes_entries);
  return UNITY_END();
}
