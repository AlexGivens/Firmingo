#include "core/session.h"
#include "core/uart.h"
#include "unity.h"
#include <algorithm>
#include <cstring>
#include <vector>

using namespace firmingo;
void setUp() {}
void tearDown() {}

struct Driver : UartDriver {
  SerialConfiguration configured{};
  std::vector<std::uint8_t> input, output;
  std::size_t offset = 0, write_limit = 256;
  bool running = false, idle = true, fail = false, overrun = false;
  unsigned configurations = 0;
  bool configure(const SerialConfiguration& value, std::uint32_t& actual) override {
    ++configurations;
    if (fail) return false;
    configured = value; actual = value.baud - 7; running = true; return true;
  }
  bool active() const override { return running; }
  bool tx_idle() const override { return idle; }
  std::size_t rx_capacity() const override { return 256; }
  std::size_t rx_reserve() const override { return 32; }
  std::size_t rx_pending() override { return input.size() - offset; }
  std::size_t read(std::uint8_t* data, std::size_t capacity) override {
    const auto count = std::min(capacity,rx_pending());
    if (count) std::memcpy(data,input.data()+offset,count);
    offset += count; return count;
  }
  std::size_t write(const std::uint8_t* data, std::size_t size) override {
    const auto count = std::min(size,write_limit);
    output.insert(output.end(),data,data+count); return count;
  }
  bool take_rx_overrun() override {
    const bool value = overrun; overrun = false; return value;
  }
};

struct ExposedUart : UartBackend {
  explicit ExposedUart(UartDriver& driver) : UartBackend(driver) {}
  std::size_t network_read(std::uint8_t* data, std::size_t size) {
    return UartBackend::read(data,size);
  }
  std::size_t network_write(const std::uint8_t* data, std::size_t size) {
    return UartBackend::write(data,size);
  }
};

static SerialConfiguration configuration(std::uint32_t baud = 115200,
                                         std::uint8_t bits = 8,
                                         SerialParity parity = SerialParity::none,
                                         std::uint8_t stops = 1) {
  SerialConfiguration value;
  value.baud=baud; value.data_bits=bits; value.parity=parity; value.stop_bits=stops;
  return value;
}

void configuration_validates_limits_and_reports_actual_hardware_baud() {
  Driver driver; ExposedUart backend(driver);
  TEST_ASSERT_FALSE(backend.connected());
  for (auto invalid : {configuration(299),configuration(2000001),configuration(9600,4),
                       configuration(9600,9),configuration(9600,8,SerialParity::none,0),
                       configuration(9600,8,SerialParity::none,3)})
    TEST_ASSERT_EQUAL_INT(int(BackendResult::invalid_argument),int(backend.configure(invalid)));
  TEST_ASSERT_EQUAL_UINT(0,driver.configurations);
  driver.fail=true;
  TEST_ASSERT_EQUAL_INT(int(BackendResult::io_error),int(backend.configure(configuration())));
  driver.fail=false;
  TEST_ASSERT_EQUAL_INT(int(BackendResult::ok),int(backend.configure(configuration())));
  SerialConfiguration observed;
  TEST_ASSERT_TRUE(backend.configuration(observed));
  TEST_ASSERT_EQUAL_UINT32(115200,observed.baud);
  TEST_ASSERT_EQUAL_UINT32(115193,observed.actual_baud);
  TEST_ASSERT_EQUAL_UINT8(8,observed.data_bits);
  TEST_ASSERT_EQUAL_UINT8(1,observed.stop_bits);
}

void reads_writes_discards_and_overruns_are_bounded_and_explicit() {
  Driver driver; ExposedUart backend(driver);
  TEST_ASSERT_EQUAL_INT(int(BackendResult::ok),int(backend.configure(configuration())));
  driver.input.resize(256);
  for (unsigned i=0;i<driver.input.size();++i) driver.input[i]=std::uint8_t(i);
  std::uint8_t bytes[256]{};
  TEST_ASSERT_EQUAL_UINT(17,backend.network_read(bytes,17));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(driver.input.data(),bytes,17);
  driver.write_limit=3;
  TEST_ASSERT_EQUAL_UINT(0,backend.network_write(driver.input.data(),20));
  std::uint8_t drained[32];
  TEST_ASSERT_EQUAL_UINT(32,backend.network_read(drained,sizeof(drained)));
  TEST_ASSERT_EQUAL_UINT(3,backend.network_write(driver.input.data(),20));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(driver.input.data(),driver.output.data(),3);
  driver.overrun=true;
  BackendDiagnostics before;
  TEST_ASSERT_TRUE(backend.diagnostics(before));
  TEST_ASSERT_EQUAL_UINT(207,before.rx_pending);
  TEST_ASSERT_EQUAL_UINT(256,before.rx_peak);
  TEST_ASSERT_EQUAL_UINT32(1,before.rx_overrun_events);
  TEST_ASSERT_EQUAL_UINT64(1,before.rx_lost_bytes_minimum);
  TEST_ASSERT_EQUAL_UINT32(1,before.tx_throttle_events);
  backend.discard();
  BackendDiagnostics after;
  TEST_ASSERT_TRUE(backend.diagnostics(after));
  TEST_ASSERT_EQUAL_UINT(0,after.rx_pending);
  TEST_ASSERT_EQUAL_UINT64(207,after.rx_discarded);
  TEST_ASSERT_EQUAL_UINT(256,after.rx_peak);
}

void channel_configures_before_open_and_rejects_busy_reconfiguration() {
  Driver driver; ExposedUart backend(driver);
  TEST_ASSERT_EQUAL_INT(int(BackendResult::ok),int(backend.configure(configuration())));
  driver.input={1,2,3,4};
  Channel channel(backend);
  const auto changed=configuration(57600,7,SerialParity::even,2);
  TEST_ASSERT_EQUAL_INT(int(StreamResult::ok),int(channel.acquire(9,0,&changed)));
  TEST_ASSERT_EQUAL_UINT32(9,channel.owner());
  BackendDiagnostics diagnostics;
  TEST_ASSERT_TRUE(backend.diagnostics(diagnostics));
  TEST_ASSERT_EQUAL_UINT64(4,diagnostics.rx_discarded);
  struct Peer : ByteIO {
    bool connected() const override { return true; }
    std::size_t read(std::uint8_t*,std::size_t) override { return 0; }
    std::size_t write(const std::uint8_t*,std::size_t) override { return 0; }
  } peer;
  driver.input={5}; driver.offset=0;
  TEST_ASSERT_EQUAL_INT(int(StreamResult::ok),int(channel.poll(9,peer,1)));
  TEST_ASSERT_EQUAL_UINT(1,channel.pending_tx());
  TEST_ASSERT_EQUAL_INT(int(BackendResult::busy),int(channel.configure(9,configuration(38400))));
  channel.release(9);
  driver.offset=driver.input.size(); driver.idle=false;
  TEST_ASSERT_EQUAL_INT(int(StreamResult::busy),int(channel.acquire(10,2)));
  driver.idle=true;
  TEST_ASSERT_EQUAL_INT(int(StreamResult::ok),int(channel.acquire(10,3)));
  channel.release(10);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(configuration_validates_limits_and_reports_actual_hardware_baud);
  RUN_TEST(reads_writes_discards_and_overruns_are_bounded_and_explicit);
  RUN_TEST(channel_configures_before_open_and_rejects_busy_reconfiguration);
  return UNITY_END();
}
