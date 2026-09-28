#include "mailbox.h"
#include "unity.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

using firmingo_managed::SpscByteRing;
void setUp() {}
void tearDown() {}

void saturation_and_wrap_preserve_exact_bytes() {
  SpscByteRing ring;
  std::array<std::uint8_t, 400> input{}, output{};
  for (std::size_t i = 0; i < input.size(); ++i)
    input[i] = std::uint8_t((i * 43u) ^ (i >> 1));
  TEST_ASSERT_EQUAL_UINT(256, ring.push(input.data(), 300));
  TEST_ASSERT_EQUAL_UINT(0, ring.free_space());
  TEST_ASSERT_EQUAL_UINT(0, ring.push(input.data() + 256, 1));
  TEST_ASSERT_EQUAL_UINT(173, ring.pop(output.data(), 173));
  TEST_ASSERT_EQUAL_UINT(83, ring.size());
  TEST_ASSERT_EQUAL_UINT(144, ring.push(input.data() + 256, 144));
  TEST_ASSERT_EQUAL_UINT(227, ring.pop(output.data() + 173, 227));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(input.data(), output.data(), input.size());
  TEST_ASSERT_EQUAL_UINT(0, ring.size());
}

void null_buffer_does_not_advance_indexes() {
  SpscByteRing ring;
  std::uint8_t value = 7;
  TEST_ASSERT_EQUAL_UINT(0, ring.push(nullptr, 1));
  TEST_ASSERT_EQUAL_UINT(1, ring.push(&value, 1));
  TEST_ASSERT_EQUAL_UINT(0, ring.pop(nullptr, 1));
  TEST_ASSERT_EQUAL_UINT(1, ring.size());
  value = 0;
  TEST_ASSERT_EQUAL_UINT(1, ring.pop(&value, 1));
  TEST_ASSERT_EQUAL_UINT8(7, value);
}

void concurrent_producer_consumer_preserve_order_under_backpressure() {
  SpscByteRing ring;
  constexpr std::size_t total = 200000;
  std::atomic<bool> failed{false};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  auto expected = [](std::size_t offset) {
    return std::uint8_t((offset * 73u) ^ (offset >> 5));
  };
  std::thread consumer([&] {
    std::size_t offset = 0;
    std::array<std::uint8_t,37> buffer{};
    while (offset < total && !failed.load()) {
      const auto count = ring.pop(buffer.data(), buffer.size());
      for (std::size_t i = 0; i < count; ++i)
        if (buffer[i] != expected(offset + i)) failed.store(true);
      offset += count;
      if (!count) std::this_thread::yield();
      if (std::chrono::steady_clock::now() > deadline) failed.store(true);
    }
    if (offset != total) failed.store(true);
  });
  std::size_t offset = 0;
  std::array<std::uint8_t,41> buffer{};
  while (offset < total && !failed.load()) {
    const auto wanted = std::min(buffer.size(), total - offset);
    for (std::size_t i = 0; i < wanted; ++i) buffer[i] = expected(offset + i);
    const auto count = ring.push(buffer.data(), wanted);
    offset += count;
    if (!count) std::this_thread::yield();
    if (std::chrono::steady_clock::now() > deadline) failed.store(true);
  }
  consumer.join();
  TEST_ASSERT_FALSE(failed.load());
  TEST_ASSERT_EQUAL_UINT(total, offset);
  TEST_ASSERT_EQUAL_UINT(0, ring.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(saturation_and_wrap_preserve_exact_bytes);
  RUN_TEST(null_buffer_does_not_advance_indexes);
  RUN_TEST(concurrent_producer_consumer_preserve_order_under_backpressure);
  return UNITY_END();
}
