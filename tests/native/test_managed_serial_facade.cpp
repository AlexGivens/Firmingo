#include "abi.h"
#include "unity.h"

#include <cstdint>
#include <vector>

namespace {
std::vector<std::uint8_t> written;
bool open = false;
std::uint8_t next_input = 0;
std::uint32_t write_bytes(const std::uint8_t* data, std::uint32_t count) {
  if (!open) return 0;
  const auto accepted = count < 5 ? count : 5;
  written.insert(written.end(), data, data + accepted);
  return accepted;
}
std::uint32_t write_available() { return open ? 5 : 0; }
std::int32_t read_byte() { return open ? next_input++ : -1; }
std::uint32_t available() { return open ? 1 : 0; }
}  // namespace

const FirmingoSketchApiV1 test_api = {
    kFirmingoApiMagic, 1, sizeof(FirmingoSketchApiV1), nullptr, nullptr,
    nullptr, nullptr, write_bytes, write_available, read_byte, available};
#define FIRMINGO_API_ADDRESS (&test_api)
#include "facade/Arduino.h"

void setUp() { written.clear(); open = false; next_input = 0; }
void tearDown() {}

void serial_facade_preserves_bytes_and_reports_short_writes() {
  Serial.begin(115200);
  TEST_ASSERT_TRUE(bool(Serial));
  TEST_ASSERT_EQUAL_UINT(0, Serial.availableForWrite());
  TEST_ASSERT_EQUAL_UINT(0, Serial.print("pre-open"));
  TEST_ASSERT_TRUE(written.empty());
  open = true;
  TEST_ASSERT_EQUAL_UINT(5, Serial.availableForWrite());
  const std::uint8_t binary[] = {0, '\r', '\n', 0x80, 0xff};
  TEST_ASSERT_EQUAL_UINT(sizeof(binary), Serial.write(binary, sizeof(binary)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(binary, written.data(), sizeof(binary));
  TEST_ASSERT_EQUAL_UINT(5, Serial.print("Hello world"));
  TEST_ASSERT_EQUAL_UINT(4, Serial.println("ok"));
  const std::uint8_t expected_tail[] = {'H', 'e', 'l', 'l', 'o', 'o', 'k', '\r', '\n'};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_tail,
                                written.data() + sizeof(binary),
                                sizeof(expected_tail));
  TEST_ASSERT_EQUAL_UINT(1, Serial.available());
  next_input = 0xff;
  TEST_ASSERT_EQUAL_INT(0xff, Serial.read());
  open = false;
  TEST_ASSERT_EQUAL_INT(-1, Serial.read());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(serial_facade_preserves_bytes_and_reports_short_writes);
  return UNITY_END();
}
