#include "core/stream.h"
#include "unity.h"
#include <algorithm>
#include <array>
#include <cstring>

using firmingo::ByteIO;
using firmingo::Stream;
using firmingo::StreamResult;

// Bounded fake I/O; exercise the production state machine, not another pump.
struct FakeIO : ByteIO {
  std::array<std::uint8_t, 4096> input{}, output{};
  std::size_t input_size = 0, consumed = 0, output_size = 0;
  std::size_t read_limit = 4096, write_limit = 4096;
  unsigned reads = 0, writes = 0;
  bool online = true, bad_read = false, bad_write = false;
  bool connected() const override { return online; }
  std::size_t read(std::uint8_t* data, std::size_t capacity) override {
    ++reads;
    if (bad_read) return capacity + 1;
    const auto count = std::min({capacity, read_limit, input_size - consumed});
    std::memcpy(data, input.data() + consumed, count);
    consumed += count;
    return count;
  }
  std::size_t write(const std::uint8_t* data, std::size_t size) override {
    ++writes;
    if (bad_write) return size + 1;
    const auto count = std::min({size, write_limit, output.size() - output_size});
    std::memcpy(output.data() + output_size, data, count);
    output_size += count;
    return count;
  }
  void fill(std::size_t size, unsigned salt = 0) {
    input_size = size;
    for (std::size_t i = 0; i < size; ++i) input[i] = static_cast<std::uint8_t>(i ^ salt);
  }
};
void setUp() {}
void tearDown() {}
static void result(StreamResult expected, StreamResult actual) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual));
}

void binary_full_duplex_survives_every_partial_write_size() {
  for (std::size_t chunk = 1; chunk <= Stream::capacity; ++chunk) {
    Stream stream;
    FakeIO peer, backend;
    peer.fill(1024);
    backend.fill(1024, 0xa5);
    peer.write_limit = chunk;
    backend.write_limit = 257 - chunk;
    peer.read_limit = 13;
    backend.read_limit = 37;
    result(StreamResult::ok, stream.open(1, 0));
    for (unsigned tick = 0; tick < 2048; ++tick) {
      result(StreamResult::ok, stream.poll(1, peer, backend, tick));
      if (peer.output_size == 1024 && backend.output_size == 1024) break;
    }
    TEST_ASSERT_EQUAL_UINT(1024, peer.output_size);
    TEST_ASSERT_EQUAL_UINT(1024, backend.output_size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(peer.input.data(), backend.output.data(), 1024);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(backend.input.data(), peer.output.data(), 1024);
  }
}

void stalled_reader_is_bounded_and_other_direction_progresses() {
  Stream stream;
  FakeIO peer, backend;
  peer.fill(4096);
  backend.fill(1024);
  backend.write_limit = 0;
  result(StreamResult::ok, stream.open(7, 0));
  for (unsigned i = 0; i < 20; ++i) result(StreamResult::ok, stream.poll(7, peer, backend, i));
  TEST_ASSERT_EQUAL_UINT(256, peer.consumed);
  TEST_ASSERT_EQUAL_UINT(1, peer.reads);
  TEST_ASSERT_EQUAL_UINT(256, stream.pending_to_backend());
  TEST_ASSERT_EQUAL_UINT(1024, peer.output_size);
  backend.write_limit = 1;
  result(StreamResult::ok, stream.poll(7, peer, backend, 20));
  TEST_ASSERT_EQUAL_UINT(255, stream.pending_to_backend());
  TEST_ASSERT_EQUAL_UINT(1, backend.output_size);
}

void second_owner_cannot_steal_or_discard_pending_bytes() {
  Stream stream;
  FakeIO peer, backend;
  peer.fill(256);
  backend.write_limit = 0;
  result(StreamResult::invalid_owner, stream.open(0, 0));
  result(StreamResult::ok, stream.open(1, 0));
  result(StreamResult::ok, stream.poll(1, peer, backend, 0));
  result(StreamResult::busy, stream.open(2, 1));
  result(StreamResult::busy, stream.open(1, 1));
  result(StreamResult::invalid_owner, stream.close(2));
  result(StreamResult::invalid_owner, stream.poll(2, peer, backend, 1));
  TEST_ASSERT_EQUAL_UINT(256, stream.pending_to_backend());
  TEST_ASSERT_EQUAL_UINT(1, stream.owner());
}

void disconnect_discards_both_queues_before_reconnect() {
  Stream stream;
  FakeIO peer, backend;
  peer.fill(256);
  backend.fill(256);
  peer.write_limit = backend.write_limit = 0;
  result(StreamResult::ok, stream.open(1, 0));
  result(StreamResult::ok, stream.poll(1, peer, backend, 0));
  peer.online = false;
  result(StreamResult::disconnected, stream.poll(1, peer, backend, 1));
  TEST_ASSERT_EQUAL_UINT(0, stream.owner());
  TEST_ASSERT_EQUAL_UINT(0, stream.pending_to_backend());
  TEST_ASSERT_EQUAL_UINT(0, stream.pending_to_peer());
  FakeIO next_peer, next_backend;
  result(StreamResult::ok, stream.open(2, 2));
  result(StreamResult::ok, stream.poll(2, next_peer, next_backend, 2));
  TEST_ASSERT_EQUAL_UINT(0, next_peer.output_size);
  TEST_ASSERT_EQUAL_UINT(0, next_backend.output_size);
  result(StreamResult::ok, stream.close(2));
}

void idle_timeout_handles_clock_rollover_and_exact_boundary() {
  Stream stream(20);
  FakeIO peer, backend;
  result(StreamResult::ok, stream.open(1, 0xfffffff0u));
  result(StreamResult::ok, stream.poll(1, peer, backend, 3));
  result(StreamResult::timeout, stream.poll(1, peer, backend, 4));
  TEST_ASSERT_EQUAL_UINT(0, stream.owner());
}

void real_progress_extends_timeout_but_stalls_do_not() {
  Stream stream(10);
  FakeIO peer, backend;
  peer.fill(256);
  backend.write_limit = 1;
  result(StreamResult::ok, stream.open(1, 0));
  result(StreamResult::ok, stream.poll(1, peer, backend, 9));
  result(StreamResult::ok, stream.poll(1, peer, backend, 18));
  backend.write_limit = 0;
  result(StreamResult::ok, stream.poll(1, peer, backend, 27));
  result(StreamResult::timeout, stream.poll(1, peer, backend, 28));
  TEST_ASSERT_EQUAL_UINT(0, stream.pending_to_backend());
}

void faulty_adapter_counts_fail_closed() {
  for (bool bad_read : {false, true}) {
    Stream stream;
    FakeIO peer, backend;
    peer.fill(256);
    peer.bad_read = bad_read;
    backend.bad_write = !bad_read;
    result(StreamResult::ok, stream.open(1, 0));
    result(StreamResult::io_error, stream.poll(1, peer, backend, 0));
    TEST_ASSERT_EQUAL_UINT(0, stream.owner());
    TEST_ASSERT_EQUAL_UINT(0, stream.pending_to_backend());
  }
}

void empty_io_is_bounded_and_timeout_can_be_disabled() {
  Stream stream(0);
  FakeIO peer, backend;
  result(StreamResult::ok, stream.open(1, 0));
  result(StreamResult::ok, stream.poll(1, peer, backend, 0xffffffffu));
  TEST_ASSERT_EQUAL_UINT(1, peer.reads);
  TEST_ASSERT_EQUAL_UINT(1, backend.reads);
  TEST_ASSERT_EQUAL_UINT(0, peer.writes);
  TEST_ASSERT_EQUAL_UINT(0, backend.writes);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(binary_full_duplex_survives_every_partial_write_size);
  RUN_TEST(stalled_reader_is_bounded_and_other_direction_progresses);
  RUN_TEST(second_owner_cannot_steal_or_discard_pending_bytes);
  RUN_TEST(disconnect_discards_both_queues_before_reconnect);
  RUN_TEST(idle_timeout_handles_clock_rollover_and_exact_boundary);
  RUN_TEST(real_progress_extends_timeout_but_stalls_do_not);
  RUN_TEST(faulty_adapter_counts_fail_closed);
  RUN_TEST(empty_io_is_bounded_and_timeout_can_be_disabled);
  return UNITY_END();
}
