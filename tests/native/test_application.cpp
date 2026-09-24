#include "core/application.h"
#include "core/session.h"
#include "unity.h"
#include <array>
#include <cstring>

using namespace firmingo;
void setUp() {}
void tearDown() {}

class ExposedEndpoint : public ApplicationEndpoint {
 public:
  std::size_t network_read(uint8_t* data, std::size_t size) {
    return ApplicationEndpoint::read(data,size);
  }
  std::size_t network_write(const uint8_t* data, std::size_t size) {
    return ApplicationEndpoint::write(data,size);
  }
};

void inactive_endpoint_never_queues_application_or_network_bytes() {
  ExposedEndpoint endpoint; uint8_t byte = 7, out = 0;
  TEST_ASSERT_TRUE(endpoint.connected());
  TEST_ASSERT_EQUAL_UINT(0,endpoint.write_to_host(&byte,1));
  TEST_ASSERT_EQUAL_UINT(0,endpoint.read_from_host(&out,1));
  TEST_ASSERT_EQUAL_UINT(0,endpoint.network_write(&byte,1));
  TEST_ASSERT_EQUAL_UINT(0,endpoint.network_read(&out,1));
  TEST_ASSERT_EQUAL_UINT(0,endpoint.available());
  TEST_ASSERT_EQUAL_UINT(0,endpoint.pending_to_host());
}

void binary_queues_wrap_and_apply_partial_backpressure_without_loss() {
  ExposedEndpoint endpoint; endpoint.opened(3);
  std::array<uint8_t,400> input{},output{};
  for (std::size_t i=0;i<input.size();++i) input[i]=uint8_t((i*73)^(i>>2));
  TEST_ASSERT_EQUAL_UINT(256,endpoint.network_write(input.data(),300));
  TEST_ASSERT_EQUAL_UINT(256,endpoint.available());
  TEST_ASSERT_EQUAL_UINT(0,endpoint.network_write(input.data()+256,1));
  TEST_ASSERT_EQUAL_UINT(173,endpoint.read_from_host(output.data(),173));
  TEST_ASSERT_EQUAL_UINT(83,endpoint.available());
  TEST_ASSERT_EQUAL_UINT(144,endpoint.network_write(input.data()+256,144));
  TEST_ASSERT_EQUAL_UINT(227,endpoint.read_from_host(output.data()+173,227));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(input.data(),output.data(),400);

  TEST_ASSERT_EQUAL_UINT(256,endpoint.write_to_host(input.data(),300));
  TEST_ASSERT_EQUAL_UINT(91,endpoint.network_read(output.data(),91));
  TEST_ASSERT_EQUAL_UINT(91,endpoint.write_to_host(input.data()+256,144));
  TEST_ASSERT_EQUAL_UINT(256,endpoint.network_read(output.data()+91,256));
  TEST_ASSERT_EQUAL_UINT(53,endpoint.write_to_host(input.data()+347,53));
  TEST_ASSERT_EQUAL_UINT(53,endpoint.network_read(output.data()+347,53));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(input.data(),output.data(),400);
}

void lifecycle_discards_queues_without_replay_and_preserves_peaks() {
  ExposedEndpoint endpoint; Channel channel(endpoint);
  TEST_ASSERT_EQUAL_INT(int(StreamResult::ok),int(channel.acquire(1,10)));
  TEST_ASSERT_TRUE(endpoint.active()); TEST_ASSERT_EQUAL_UINT32(1,endpoint.owner());
  TEST_ASSERT_EQUAL_UINT32(1,endpoint.generation());
  std::array<uint8_t,100> bytes{};
  TEST_ASSERT_EQUAL_UINT(100,endpoint.network_write(bytes.data(),bytes.size()));
  TEST_ASSERT_EQUAL_UINT(100,endpoint.write_to_host(bytes.data(),bytes.size()));
  channel.release(1);
  TEST_ASSERT_FALSE(endpoint.active()); TEST_ASSERT_EQUAL_UINT32(0,endpoint.owner());
  TEST_ASSERT_EQUAL_UINT64(100,endpoint.discarded_from_host());
  TEST_ASSERT_EQUAL_UINT64(100,endpoint.discarded_to_host());
  TEST_ASSERT_EQUAL_INT(int(StreamResult::ok),int(channel.acquire(2,20)));
  TEST_ASSERT_EQUAL_UINT32(2,endpoint.generation());
  uint8_t out; TEST_ASSERT_EQUAL_UINT(0,endpoint.read_from_host(&out,1));
  TEST_ASSERT_EQUAL_UINT(0,endpoint.network_read(&out,1));
  BackendDiagnostics d; TEST_ASSERT_TRUE(endpoint.diagnostics(d));
  TEST_ASSERT_EQUAL_UINT(0,d.rx_pending); TEST_ASSERT_EQUAL_UINT(0,d.tx_pending);
  TEST_ASSERT_EQUAL_UINT(100,d.rx_peak); TEST_ASSERT_EQUAL_UINT(100,d.tx_peak);
}

void channel_disconnect_notifies_endpoint_and_counts_only_discarded_bytes() {
  struct Peer : ByteIO {
    bool online=true;
    bool connected() const override { return online; }
    std::size_t read(uint8_t*,std::size_t) override { return 0; }
    std::size_t write(const uint8_t*,std::size_t) override { return 0; }
  } peer;
  ExposedEndpoint endpoint; Channel channel(endpoint);
  TEST_ASSERT_EQUAL_INT(int(StreamResult::ok),int(channel.acquire(7,0)));
  std::array<uint8_t,30> bytes{};
  endpoint.network_write(bytes.data(),17); endpoint.write_to_host(bytes.data(),30);
  peer.online=false;
  TEST_ASSERT_EQUAL_INT(int(StreamResult::disconnected),int(channel.poll(7,peer,1)));
  TEST_ASSERT_FALSE(endpoint.active()); TEST_ASSERT_EQUAL_UINT32(0,channel.owner());
  TEST_ASSERT_EQUAL_UINT64(17,endpoint.discarded_from_host());
  TEST_ASSERT_EQUAL_UINT64(30,endpoint.discarded_to_host());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(inactive_endpoint_never_queues_application_or_network_bytes);
  RUN_TEST(binary_queues_wrap_and_apply_partial_backpressure_without_loss);
  RUN_TEST(lifecycle_discards_queues_without_replay_and_preserves_peaks);
  RUN_TEST(channel_disconnect_notifies_endpoint_and_counts_only_discarded_bytes);
  return UNITY_END();
}
