#include "console.h"
#include "unity.h"

#include <array>
#include <cstdint>

using firmingo_managed::SketchConsole;

void setUp() {}
void tearDown() {}

class TestEndpoint final : public firmingo::ApplicationEndpoint {
 public:
  std::size_t network_write(const std::uint8_t* bytes, std::size_t count) {
    return write(bytes, count);
  }
  std::size_t network_read(std::uint8_t* bytes, std::size_t count) {
    return read(bytes, count);
  }
};

void activate(SketchConsole& console, TestEndpoint& backend,
              std::array<std::uint8_t, SketchConsole::capacity>& scratch,
              std::uint32_t owner) {
  backend.opened(owner);
  TEST_ASSERT_TRUE(console.poll(backend, scratch.data(), scratch.size()));
  TEST_ASSERT_EQUAL_UINT(0, console.sketch_write_available());
  console.sketch_boundary();
  TEST_ASSERT_TRUE(console.poll(backend, scratch.data(), scratch.size()));
  TEST_ASSERT_EQUAL_UINT(SketchConsole::capacity,
                         console.sketch_write_available());
}

void binary_bytes_and_short_writes_use_bounded_queues() {
  SketchConsole console;
  TestEndpoint backend;
  std::array<std::uint8_t, SketchConsole::capacity> scratch{}, input{}, output{};
  console.sketch_boundary();
  TEST_ASSERT_EQUAL_UINT(1, console.counters().loop_boundaries);
  TEST_ASSERT_EQUAL_UINT(0, console.sketch_write(input.data(), 3));
  TEST_ASSERT_EQUAL_UINT(3, console.counters().output_rejected);
  activate(console, backend, scratch, 1);
  TEST_ASSERT_EQUAL_UINT(console.counters().epoch,
                         console.counters().acknowledged_epoch);
  TEST_ASSERT_TRUE(console.counters().output_enabled);

  for (std::size_t i = 0; i < input.size(); ++i) input[i] = std::uint8_t(i);
  TEST_ASSERT_EQUAL_UINT(256, console.sketch_write(input.data(), input.size()));
  TEST_ASSERT_EQUAL_UINT(0, console.sketch_write(input.data(), 1));
  TEST_ASSERT_EQUAL_UINT(256, console.counters().output_peak);
  TEST_ASSERT_EQUAL_UINT(4, console.counters().output_rejected);
  TEST_ASSERT_TRUE(console.poll(backend, scratch.data(), scratch.size()));
  TEST_ASSERT_EQUAL_UINT(256, backend.network_read(output.data(), output.size()));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(input.data(), output.data(), input.size());

  TEST_ASSERT_EQUAL_UINT(256, backend.network_write(input.data(), input.size()));
  TEST_ASSERT_TRUE(console.poll(backend, scratch.data(), scratch.size()));
  TEST_ASSERT_EQUAL_UINT(256, console.sketch_available());
  for (std::size_t i = 0; i < input.size(); ++i)
    TEST_ASSERT_EQUAL_INT(input[i], console.sketch_read());
  TEST_ASSERT_EQUAL_INT(-1, console.sketch_read());
}

void slow_reader_keeps_order_and_does_not_block_input() {
  SketchConsole console;
  TestEndpoint backend;
  std::array<std::uint8_t, SketchConsole::capacity> scratch{}, filled{}, out{};
  activate(console, backend, scratch, 1);
  filled.fill(0x31);
  TEST_ASSERT_EQUAL_UINT(256, backend.write_to_host(filled.data(), filled.size()));
  const std::uint8_t sketch_data[] = {0, '\r', '\n', 0x80, 0xff};
  TEST_ASSERT_EQUAL_UINT(sizeof(sketch_data),
                         console.sketch_write(sketch_data, sizeof(sketch_data)));
  TEST_ASSERT_TRUE(console.poll(backend, scratch.data(), scratch.size()));
  TEST_ASSERT_EQUAL_UINT(256 - sizeof(sketch_data),
                         console.sketch_write_available());
  const std::uint8_t host_data[] = {0xff, 0, '\n'};
  TEST_ASSERT_EQUAL_UINT(sizeof(host_data),
                         backend.network_write(host_data, sizeof(host_data)));
  TEST_ASSERT_TRUE(console.poll(backend, scratch.data(), scratch.size()));
  for (auto expected : host_data)
    TEST_ASSERT_EQUAL_INT(expected, console.sketch_read());
  TEST_ASSERT_EQUAL_UINT(256, backend.network_read(out.data(), out.size()));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(filled.data(), out.data(), out.size());
  TEST_ASSERT_TRUE(console.poll(backend, scratch.data(), scratch.size()));
  TEST_ASSERT_EQUAL_UINT(sizeof(sketch_data),
                         backend.network_read(out.data(), out.size()));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(sketch_data, out.data(), sizeof(sketch_data));
}

void reconnect_drops_old_owner_bytes_before_new_owner() {
  SketchConsole console;
  TestEndpoint backend;
  std::array<std::uint8_t, SketchConsole::capacity> scratch{}, out{};
  activate(console, backend, scratch, 11);
  const std::uint8_t old_bytes[] = {'o', 'l', 'd'};
  const std::uint8_t new_bytes[] = {'n', 'e', 'w'};
  TEST_ASSERT_EQUAL_UINT(3, backend.network_write(old_bytes, 3));
  TEST_ASSERT_TRUE(console.poll(backend, scratch.data(), scratch.size()));
  TEST_ASSERT_EQUAL_UINT(3, console.sketch_available());
  TEST_ASSERT_EQUAL_UINT(3, console.sketch_write(old_bytes, 3));

  backend.closed();
  backend.discard();
  TEST_ASSERT_TRUE(console.poll(backend, scratch.data(), scratch.size()));
  TEST_ASSERT_EQUAL_UINT(0, console.sketch_write_available());
  TEST_ASSERT_EQUAL_INT(-1, console.sketch_read());
  backend.opened(12);
  TEST_ASSERT_TRUE(console.poll(backend, scratch.data(), scratch.size()));
  TEST_ASSERT_EQUAL_UINT(0, console.sketch_write_available());
  TEST_ASSERT_NOT_EQUAL(console.counters().epoch,
                        console.counters().acknowledged_epoch);
  console.sketch_boundary();
  TEST_ASSERT_TRUE(console.poll(backend, scratch.data(), scratch.size()));
  TEST_ASSERT_EQUAL_UINT(console.counters().epoch,
                         console.counters().acknowledged_epoch);
  TEST_ASSERT_EQUAL_UINT(3, console.counters().input_discarded);
  TEST_ASSERT_EQUAL_UINT(3, console.counters().output_discarded);
  TEST_ASSERT_EQUAL_UINT(3, console.sketch_write(new_bytes, 3));
  TEST_ASSERT_TRUE(console.poll(backend, scratch.data(), scratch.size()));
  TEST_ASSERT_EQUAL_UINT(3, backend.network_read(out.data(), out.size()));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(new_bytes, out.data(), 3);
  TEST_ASSERT_EQUAL_UINT(0, backend.network_read(out.data(), out.size()));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(binary_bytes_and_short_writes_use_bounded_queues);
  RUN_TEST(slow_reader_keeps_order_and_does_not_block_input);
  RUN_TEST(reconnect_drops_old_owner_bytes_before_new_owner);
  return UNITY_END();
}
