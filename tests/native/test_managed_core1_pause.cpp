#include "core1_pause.h"
#include "unity.h"

#include <chrono>
#include <atomic>
#include <thread>

using firmingo_managed::Core1Pause;
void setUp() {}
void tearDown() {}

void idle_core_never_reports_a_flash_safe_park() {
  Core1Pause pause;
  pause.park_if_requested();
  TEST_ASSERT_FALSE(pause.parked());
  TEST_ASSERT_TRUE(pause.request());
  TEST_ASSERT_FALSE(pause.parked());
  pause.release();
  TEST_ASSERT_FALSE(pause.parked());
}

void request_is_acknowledged_only_inside_the_park_and_release_resumes() {
  Core1Pause pause;
  TEST_ASSERT_TRUE(pause.request());
  TEST_ASSERT_FALSE(pause.request());
  bool finished = false;
  std::thread core1([&] { pause.park_if_requested(); finished = true; });
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  bool observed = false;
  while (std::chrono::steady_clock::now() < deadline) {
    if (pause.parked()) { observed = true; break; }
    std::this_thread::yield();
  }
  pause.release();
  core1.join();
  TEST_ASSERT_TRUE(observed);
  TEST_ASSERT_TRUE(finished);
  TEST_ASSERT_FALSE(pause.parked());
  TEST_ASSERT_TRUE(pause.request());
  pause.release();
}

void cancelled_or_stalled_request_never_authorizes_flash() {
  Core1Pause pause;
  TEST_ASSERT_TRUE(pause.request());
  // A stalled sketch has not returned to the resident loop: no acknowledgement.
  TEST_ASSERT_FALSE(pause.parked());
  pause.release();
  std::thread core1([&] { pause.park_if_requested(); });
  core1.join();
  TEST_ASSERT_FALSE(pause.parked());
}

void repeated_handshakes_do_not_reenter_an_old_park() {
  Core1Pause pause;
  std::atomic<bool> stop{false};
  std::thread core1([&] {
    while (!stop.load()) {
      pause.park_if_requested();
      std::this_thread::yield();
    }
  });
  bool passed = true;
  for (unsigned cycle = 0; cycle < 100 && passed; ++cycle) {
    if (!pause.request()) { passed = false; break; }
    const auto enter_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!pause.parked() && std::chrono::steady_clock::now() < enter_deadline)
      std::this_thread::yield();
    if (!pause.parked()) passed = false;
    pause.release();
    const auto leave_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (pause.parked() && std::chrono::steady_clock::now() < leave_deadline)
      std::this_thread::yield();
    if (pause.parked()) passed = false;
  }
  pause.release();
  stop.store(true);
  core1.join();
  TEST_ASSERT_TRUE(passed);
  TEST_ASSERT_FALSE(pause.parked());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(idle_core_never_reports_a_flash_safe_park);
  RUN_TEST(request_is_acknowledged_only_inside_the_park_and_release_resumes);
  RUN_TEST(cancelled_or_stalled_request_never_authorizes_flash);
  RUN_TEST(repeated_handshakes_do_not_reenter_an_old_park);
  return UNITY_END();
}
