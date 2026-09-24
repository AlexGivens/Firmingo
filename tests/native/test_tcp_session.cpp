#include "ports/arduino_pico/tcp_session.h"
#include "core/loopback.h"
#include "unity.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using namespace firmingo;
static tcp_pcb listener;
struct Wire {
  std::vector<uint8_t> bytes;
  std::size_t acked = 0;
  unsigned writes = 0, outputs = 0, closes = 0;
  err_t write_error = ERR_OK, close_error = ERR_OK;
};
static std::map<tcp_pcb*, Wire> wires;
static unsigned freed, credited, timer_calls;
static void (*timer_event)();
static uint32_t tick;
static bool allocation_ok, listen_ok;
static err_t bind_error;
static Identity identity = {"a1b2c3d4e5f60718", "0123456789abcdef", "nano_rp2040_connect", "session-dev-v1"};
static uint32_t clock_ms() { return tick; }
tcp_pcb* tcp_new_ip_type(int) { return allocation_ok ? &listener : nullptr; }
err_t tcp_bind(tcp_pcb*, const ip_addr_t*, uint16_t) { return bind_error; }
tcp_pcb* tcp_listen_with_backlog(tcp_pcb* pcb, int backlog) {
  TEST_ASSERT_EQUAL_UINT(2, backlog); return listen_ok ? pcb : nullptr;
}
void tcp_abort(tcp_pcb* pcb) { TEST_ASSERT_NOT_NULL(pcb); pcb->aborted = true; }
err_t tcp_close(tcp_pcb* pcb) { ++wires[pcb].closes; return wires[pcb].close_error; }
void tcp_recved(tcp_pcb*, uint16_t count) { credited += count; }
err_t tcp_write(tcp_pcb* pcb, const void* data, uint16_t count, int flags) {
  auto& wire = wires[pcb]; ++wire.writes;
  TEST_ASSERT_EQUAL_INT(TCP_WRITE_FLAG_COPY, flags);
  TEST_ASSERT_TRUE(count <= pcb->room);
  if (wire.write_error != ERR_OK) return wire.write_error;
  const auto* bytes = static_cast<const uint8_t*>(data);
  wire.bytes.insert(wire.bytes.end(), bytes, bytes + count);
  pcb->room -= count;
  return ERR_OK;
}
err_t tcp_output(tcp_pcb* pcb) { ++wires[pcb].outputs; return ERR_OK; }
void pbuf_free(pbuf* packet) { for (; packet; packet = packet->next) ++freed; }
uint16_t pbuf_copy_partial(pbuf* packet, void* output, uint16_t count, uint16_t offset) {
  auto* bytes = static_cast<uint8_t*>(output);
  uint16_t copied = 0;
  for (; packet && copied < count; packet = packet->next) {
    const uint16_t length = packet->len ? packet->len : packet->tot_len;
    if (offset >= length) { offset -= length; continue; }
    const auto n = static_cast<uint16_t>(std::min<unsigned>(count - copied, length - offset));
    std::memcpy(bytes + copied, packet->payload + offset, n);
    copied += n; offset = 0;
  }
  return copied;
}
void sys_check_timeouts() { ++timer_calls; if(timer_event) timer_event(); }
void setUp() {
  listener = tcp_pcb{}; wires.clear(); freed = credited = tick = timer_calls = 0; timer_event=nullptr;
  allocation_ok = listen_ok = true; bind_error = ERR_OK;
}
void tearDown() {}
static void start(TcpSessionServer& server) { TEST_ASSERT_TRUE(server.begin(nullptr, 7420, identity, clock_ms)); }
static void connect(tcp_pcb& peer) { TEST_ASSERT_EQUAL_INT(ERR_OK, listener.accept(listener.arg, &peer, ERR_OK)); }
static void ack(tcp_pcb& peer) {
  auto& wire = wires[&peer];
  const auto count = static_cast<uint16_t>(wire.bytes.size() - wire.acked);
  if (!count || !peer.sent) return;
  wire.acked += count; peer.room += count;
  TEST_ASSERT_EQUAL_INT(ERR_OK, peer.sent(peer.arg, &peer, count));
}
static std::vector<uint8_t> frame(protocol::Type type, uint32_t id, const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> bytes(protocol::header_size + payload.size());
  TEST_ASSERT_EQUAL_UINT(bytes.size(), protocol::encode(bytes.data(), bytes.size(), type, id, payload.data(), payload.size()));
  return bytes;
}
static std::vector<uint8_t> request(const char* json, uint32_t id = 1) {
  return frame(protocol::Type::request, id, std::vector<uint8_t>(json, json + std::strlen(json)));
}
static void send(TcpSessionServer& server, tcp_pcb& peer, std::vector<uint8_t> bytes) {
  const unsigned before = freed;
  pbuf packet{static_cast<uint16_t>(bytes.size()), bytes.data()};
  TEST_ASSERT_EQUAL_INT(ERR_OK, peer.recv(peer.arg, &peer, &packet, ERR_OK));
  for (unsigned i = 0; i < 2000 && freed == before; ++i) { server.poll(++tick); ack(peer); }
  TEST_ASSERT_EQUAL_UINT(before + 1, freed);
  for (unsigned i = 0; i < 40 && !peer.aborted && peer.arg; ++i) { server.poll(++tick); ack(peer); }
}
static void negotiate(TcpSessionServer& server, tcp_pcb& peer, bool acquire = true) {
  send(server, peer, request("{\"op\":\"hello\"}"));
  if (acquire) send(server, peer, request("{\"op\":\"serial.open\",\"channel_id\":1}", 2));
}
static std::vector<uint8_t> returned_data(tcp_pcb& peer) {
  protocol::Decoder decoder;
  const auto& wire = wires[&peer].bytes;
  std::vector<uint8_t> result;
  std::size_t offset = 0;
  while (offset < wire.size()) {
    offset += decoder.feed(wire.data() + offset, wire.size() - offset);
    TEST_ASSERT_TRUE(decoder.state() == protocol::Decode::incomplete || decoder.state() == protocol::Decode::ready);
    if (decoder.state() != protocol::Decode::ready) continue;
    if (decoder.type() == protocol::Type::serial) {
      TEST_ASSERT_TRUE(decoder.size() > 4);
      const uint8_t prefix[] = {0,0,0,1};
      TEST_ASSERT_EQUAL_UINT8_ARRAY(prefix, decoder.payload(), 4);
      result.insert(result.end(), decoder.payload() + 4, decoder.payload() + decoder.size());
    }
    decoder.reset();
  }
  TEST_ASSERT_EQUAL_UINT(protocol::header_size, decoder.needed());
  return result;
}
static bool contains(tcp_pcb& peer, const char* value) {
  const auto& bytes = wires[&peer].bytes;
  return std::search(bytes.begin(), bytes.end(), value, value + std::strlen(value)) != bytes.end();
}
void chained_input_partial_writes_and_memory_pressure_preserve_binary() {
  Loopback backend; Channel channel(backend); TcpSessionServer server(channel); tcp_pcb peer;
  start(server); connect(peer); negotiate(server, peer);
  std::vector<uint8_t> payload{0,0,0,1};
  for (unsigned i = 0; i < 4088; ++i) payload.push_back(static_cast<uint8_t>((i*73)^(i>>8)));
  auto bytes = frame(protocol::Type::serial, 0, payload);
  pbuf tail{static_cast<uint16_t>(bytes.size()-19), bytes.data()+19};
  pbuf head{static_cast<uint16_t>(bytes.size()), bytes.data(), &tail, 19};
  const unsigned credit_before = credited, free_before = freed;
  TEST_ASSERT_EQUAL_INT(ERR_OK, peer.recv(peer.arg, &peer, &head, ERR_OK));
  wires[&peer].write_error = ERR_MEM;
  const auto output_before = wires[&peer].bytes.size();
  for (unsigned i = 0; i < 200; ++i) server.poll(++tick);
  TEST_ASSERT_EQUAL_UINT(output_before, wires[&peer].bytes.size());
  // Decoder holds one frame; backend + stream + outbound frame are fixed-size.
  TEST_ASSERT_TRUE(channel.pending_rx() <= 256 && channel.pending_tx() <= 256);
  TEST_ASSERT_EQUAL_UINT(bytes.size(), credited-credit_before);
  TEST_ASSERT_EQUAL_UINT(free_before+2, freed);
  wires[&peer].write_error = ERR_OK; peer.room = 7;
  for (unsigned i = 0; i < 2000; ++i) { server.poll(++tick); ack(peer); }
  const auto output = returned_data(peer);
  TEST_ASSERT_EQUAL_UINT(payload.size()-4, output.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload.data()+4, output.data(), output.size());
  server.stop();
}
void coalesced_frames_stop_receive_credit_when_peer_stalls() {
  Loopback backend; Channel channel(backend); TcpSessionServer server(channel); tcp_pcb peer;
  start(server); connect(peer); negotiate(server,peer);
  std::vector<uint8_t> payload{0,0,0,1};
  for(unsigned i=0;i<1024;++i) payload.push_back(static_cast<uint8_t>(i));
  std::vector<uint8_t> bytes;
  const auto data=frame(protocol::Type::serial,0,payload);
  for(unsigned i=0;i<16;++i) bytes.insert(bytes.end(),data.begin(),data.end());
  pbuf packet{static_cast<uint16_t>(bytes.size()),bytes.data()};
  const unsigned before=credited, free_before=freed;
  peer.recv(peer.arg,&peer,&packet,ERR_OK); wires[&peer].write_error=ERR_MEM;
  for(unsigned i=0;i<200;++i) server.poll(++tick);
  TEST_ASSERT_TRUE(credited-before <= 3*data.size());
  TEST_ASSERT_EQUAL_UINT(free_before,freed);
  const auto blocked_credit=credited;
  for(unsigned i=0;i<200;++i) server.poll(++tick);
  TEST_ASSERT_EQUAL_UINT(blocked_credit,credited);
  wires[&peer].write_error=ERR_OK;
  for(unsigned i=0;i<3000;++i) { server.poll(++tick); ack(peer); }
  const auto output=returned_data(peer);
  TEST_ASSERT_EQUAL_UINT(16*1024,output.size());
  for(unsigned i=0;i<output.size();++i) TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(i),output[i]);
  TEST_ASSERT_EQUAL_UINT(before+bytes.size(),credited);
  TEST_ASSERT_EQUAL_UINT(free_before+1,freed);
  server.stop();
}
void graceful_close_retries_memory_failure_without_using_transferred_pcb() {
  Loopback backend; Channel channel(backend); TcpSessionServer server(channel); tcp_pcb peer;
  start(server); connect(peer); negotiate(server,peer);
  auto bytes=request("{broken",3); pbuf packet{static_cast<uint16_t>(bytes.size()),bytes.data()};
  peer.recv(peer.arg,&peer,&packet,ERR_OK);
  for(unsigned i=0;i<40;++i) server.poll(++tick);
  ack(peer); wires[&peer].close_error=ERR_MEM; server.poll(++tick);
  TEST_ASSERT_EQUAL_UINT(1,server.active_connections()); TEST_ASSERT_NOT_NULL(peer.arg);
  wires[&peer].close_error=ERR_OK; server.poll(++tick);
  TEST_ASSERT_EQUAL_UINT(2,wires[&peer].closes); TEST_ASSERT_EQUAL_UINT(0,server.active_connections());
  TEST_ASSERT_FALSE(peer.aborted); TEST_ASSERT_NULL(peer.arg);
  server.poll(++tick); TEST_ASSERT_EQUAL_UINT(2,wires[&peer].closes);
  server.stop();
}
void second_socket_receives_busy_without_displacing_owner() {
  Loopback backend; Channel channel(backend); TcpSessionServer server(channel); tcp_pcb owner, other, excess;
  start(server); connect(owner); negotiate(server, owner); connect(other); negotiate(server, other);
  TEST_ASSERT_TRUE(contains(other,"\"code\":\"busy\""));
  TEST_ASSERT_EQUAL_UINT(1, channel.owner());
  TEST_ASSERT_EQUAL_UINT(2, server.active_connections());
  TEST_ASSERT_EQUAL_INT(ERR_ABRT, listener.accept(listener.arg,&excess,ERR_OK));
  TEST_ASSERT_TRUE(excess.aborted); TEST_ASSERT_FALSE(owner.aborted);
  send(server, owner, frame(protocol::Type::serial,0,{0,0,0,1,0,13,10,255}));
  TEST_ASSERT_EQUAL_UINT(4, returned_data(owner).size());
  server.stop();
}
void callbacks_defer_backend_cleanup_and_new_owner_gets_no_replay() {
  for (bool eof : {false,true}) {
    setUp(); Loopback backend; Channel channel(backend); TcpSessionServer server(channel); tcp_pcb owner, next;
    start(server); connect(owner); negotiate(server,owner); connect(next); negotiate(server,next,false);
    auto bytes = frame(protocol::Type::serial,0,{0,0,0,1,42,255});
    pbuf packet{static_cast<uint16_t>(bytes.size()),bytes.data()};
    owner.room=0; owner.recv(owner.arg,&owner,&packet,ERR_OK); server.poll(++tick);
    if(eof) TEST_ASSERT_EQUAL_INT(ERR_ABRT,owner.recv(owner.arg,&owner,nullptr,ERR_OK));
    else { auto callback=owner.error; auto arg=owner.arg; owner.arg=nullptr; callback(arg,ERR_CONN); }
    TEST_ASSERT_EQUAL_UINT(1,channel.owner()); // No backend calls inside callbacks.
    server.poll(++tick);
    TEST_ASSERT_EQUAL_UINT(0,channel.owner());
    send(server,next,request("{\"op\":\"serial.open\",\"channel_id\":1}",2));
    TEST_ASSERT_EQUAL_UINT(2,channel.owner());
    TEST_ASSERT_EQUAL_UINT(0,returned_data(next).size());
    server.stop();
  }
}
void fatal_json_reply_waits_for_tcp_ack_then_closes_without_dangling_callbacks() {
  Loopback backend; Channel channel(backend); TcpSessionServer server(channel); tcp_pcb peer;
  start(server); connect(peer); negotiate(server,peer);
  auto bytes=request("{broken",3); pbuf packet{static_cast<uint16_t>(bytes.size()),bytes.data()};
  peer.recv(peer.arg,&peer,&packet,ERR_OK);
  for(unsigned i=0;i<40;++i) server.poll(++tick);
  TEST_ASSERT_TRUE(contains(peer,"\"code\":\"invalid_argument\""));
  TEST_ASSERT_EQUAL_UINT(0,channel.owner());
  TEST_ASSERT_EQUAL_UINT(1,server.active_connections());
  TEST_ASSERT_EQUAL_UINT(0,wires[&peer].closes);
  ack(peer); server.poll(++tick);
  TEST_ASSERT_EQUAL_UINT(0,server.active_connections());
  TEST_ASSERT_EQUAL_UINT(1,wires[&peer].closes);
  TEST_ASSERT_FALSE(peer.aborted);
  TEST_ASSERT_NULL(peer.arg); TEST_ASSERT_NULL(peer.recv); TEST_ASSERT_NULL(peer.error); TEST_ASSERT_NULL(peer.sent);
  server.stop();
}
void unacknowledged_terminal_output_and_close_memory_failure_have_deadlines() {
  for (bool ack_output : {false,true}) {
    setUp(); Loopback backend; Channel channel(backend); TcpSessionServer server(channel); tcp_pcb peer;
    start(server); connect(peer); negotiate(server,peer);
    auto bytes=request("{broken",3); pbuf packet{static_cast<uint16_t>(bytes.size()),bytes.data()};
    peer.recv(peer.arg,&peer,&packet,ERR_OK);
    for(unsigned i=0;i<40;++i) server.poll(++tick);
    if(ack_output) { wires[&peer].close_error=ERR_MEM; ack(peer); server.poll(++tick); TEST_ASSERT_NOT_NULL(peer.arg); }
    tick+=TcpSessionServer::close_ms; server.poll(tick);
    TEST_ASSERT_TRUE(peer.aborted); TEST_ASSERT_NULL(peer.arg);
    TEST_ASSERT_EQUAL_UINT(0,server.active_connections());
    server.stop();
  }
}
void handshake_timeout_starts_at_accept_even_before_first_application_poll() {
  Loopback backend; Channel channel(backend); TcpSessionServer server(channel); tcp_pcb peer;
  start(server); tick=0xfffffff0; connect(peer);
  tick+=Session::handshake_ms; server.poll(tick);
  TEST_ASSERT_EQUAL_UINT(0,server.active_connections());
  TEST_ASSERT_EQUAL_UINT(1,wires[&peer].closes); TEST_ASSERT_NULL(peer.arg);
  server.stop();
}
void transport_fault_releases_owner_and_refused_input_stays_stack_owned() {
  Loopback backend; Channel channel(backend); TcpSessionServer server(channel); tcp_pcb peer;
  start(server); connect(peer); negotiate(server,peer);
  auto bytes=frame(protocol::Type::serial,0,{0,0,0,1,7});
  pbuf first{static_cast<uint16_t>(bytes.size()),bytes.data()}, second=first;
  const unsigned before=freed;
  TEST_ASSERT_EQUAL_INT(ERR_OK,peer.recv(peer.arg,&peer,&first,ERR_OK));
  TEST_ASSERT_EQUAL_INT(ERR_MEM,peer.recv(peer.arg,&peer,&second,ERR_OK));
  TEST_ASSERT_EQUAL_UINT(before,freed);
  wires[&peer].write_error=ERR_CONN;
  for(unsigned i=0;i<20 && server.active_connections();++i) server.poll(++tick);
  TEST_ASSERT_TRUE(peer.aborted); TEST_ASSERT_EQUAL_UINT(0,channel.owner());
  TEST_ASSERT_EQUAL_UINT(before+1,freed); // Refused second packet still stack-owned.
  server.stop();
}
void reconnect_accept_before_application_cleanup_reuses_only_dead_socket_slot() {
  Loopback backend; Channel channel(backend); TcpSessionServer server(channel); tcp_pcb owner,other,next,excess;
  start(server); connect(owner); negotiate(server,owner); connect(other); negotiate(server,other,false);
  std::vector<uint8_t> payload(1028,42); payload[0]=payload[1]=payload[2]=0;payload[3]=1;
  auto bytes=frame(protocol::Type::serial,0,payload);const auto second=bytes;
  bytes.insert(bytes.end(),second.begin(),second.end());
  pbuf old_packet{static_cast<uint16_t>(bytes.size()),bytes.data()};
  owner.recv(owner.arg,&owner,&old_packet,ERR_OK); owner.room=0;
  for(unsigned i=0;i<40;++i)server.poll(++tick);
  TEST_ASSERT_TRUE(channel.rx_bytes()>0);
  TEST_ASSERT_EQUAL_INT(ERR_ABRT,owner.recv(owner.arg,&owner,nullptr,ERR_OK));
  TEST_ASSERT_EQUAL_INT(ERR_ABRT,other.recv(other.arg,&other,nullptr,ERR_OK));
  TEST_ASSERT_EQUAL_UINT(1,channel.owner()); // No callback touches backend queues.
  TEST_ASSERT_EQUAL_INT(ERR_OK,listener.accept(listener.arg,&next,ERR_OK));
  auto hello=request("{\"op\":\"hello\"}"); pbuf incoming{static_cast<uint16_t>(hello.size()),hello.data()};
  TEST_ASSERT_EQUAL_INT(ERR_MEM,next.recv(next.arg,&next,&incoming,ERR_OK));
  const auto credit_before_cleanup=credited;
  server.poll(++tick); TEST_ASSERT_EQUAL_UINT(0,channel.owner());
  TEST_ASSERT_EQUAL_UINT(credit_before_cleanup,credited); // Old bytes don't credit new PCB.
  TEST_ASSERT_EQUAL_INT(ERR_OK,next.recv(next.arg,&next,&incoming,ERR_OK));
  for(unsigned i=0;i<40;++i) {server.poll(++tick);ack(next);}
  TEST_ASSERT_TRUE(contains(next,"application")); TEST_ASSERT_FALSE(next.aborted);
  send(server,next,request("{\"op\":\"serial.open\",\"channel_id\":1}",2));
  TEST_ASSERT_EQUAL_UINT(0,returned_data(next).size());
  connect(excess); tcp_pcb third;
  TEST_ASSERT_EQUAL_INT(ERR_ABRT,listener.accept(listener.arg,&third,ERR_OK));
  server.stop();
}
void accept_resource_error_with_null_pcb_never_aborts_null_or_displaces_owner() {
  Loopback backend; Channel channel(backend); TcpSessionServer server(channel); tcp_pcb owner;
  start(server);connect(owner);negotiate(server,owner);
  TEST_ASSERT_EQUAL_INT(ERR_MEM,listener.accept(listener.arg,nullptr,ERR_MEM));
  TEST_ASSERT_EQUAL_UINT(1,channel.owner()); TEST_ASSERT_FALSE(owner.aborted);
  server.stop();
}
static tcp_pcb* retry_peer;
static pbuf* retry_packet;
static bool retry_delivered;
static void retry_refused_data() {
  if(!retry_packet)return;
  if(retry_peer->recv(retry_peer->arg,retry_peer,retry_packet,ERR_OK)==ERR_OK) {
    retry_packet=nullptr; retry_delivered=true;
  }
}
void frequent_application_poll_dispatches_timer_owned_refused_data() {
  Loopback backend; Channel channel(backend); TcpSessionServer server(channel); tcp_pcb owner,other,next;
  start(server);connect(owner);negotiate(server,owner);connect(other);negotiate(server,other,false);
  owner.recv(owner.arg,&owner,nullptr,ERR_OK);other.recv(other.arg,&other,nullptr,ERR_OK);
  connect(next);auto bytes=request("{\"op\":\"hello\"}");
  pbuf packet{static_cast<uint16_t>(bytes.size()),bytes.data()};
  TEST_ASSERT_EQUAL_INT(ERR_MEM,next.recv(next.arg,&next,&packet,ERR_OK));
  retry_peer=&next;retry_packet=&packet;retry_delivered=false;timer_event=retry_refused_data;
  const auto before=timer_calls;
  for(unsigned i=0;i<40;++i){server.poll(++tick);ack(next);}
  TEST_ASSERT_EQUAL_UINT(before+40,timer_calls);TEST_ASSERT_TRUE(retry_delivered);
  TEST_ASSERT_TRUE(contains(next,"application"));TEST_ASSERT_FALSE(next.aborted);
  timer_event=nullptr;server.stop();
}
void listener_failures_are_reported_and_stop_releases_live_sessions() {
  Loopback backend; Channel channel(backend); TcpSessionServer server(channel);
  TEST_ASSERT_FALSE(server.begin(nullptr,7420,identity,nullptr));
  allocation_ok=false; TEST_ASSERT_FALSE(server.begin(nullptr,7420,identity,clock_ms));
  allocation_ok=true; bind_error=ERR_CONN; TEST_ASSERT_FALSE(server.begin(nullptr,7420,identity,clock_ms));
  TEST_ASSERT_TRUE(listener.aborted);
  listener=tcp_pcb{}; bind_error=ERR_OK; listen_ok=false;
  TEST_ASSERT_FALSE(server.begin(nullptr,7420,identity,clock_ms)); TEST_ASSERT_TRUE(listener.aborted);
  listener=tcp_pcb{}; listen_ok=true; start(server);
  TEST_ASSERT_FALSE(server.begin(nullptr,7420,identity,clock_ms));
  tcp_pcb peer; connect(peer); negotiate(server,peer);
  server.stop(); TEST_ASSERT_EQUAL_UINT(0,channel.owner()); TEST_ASSERT_TRUE(peer.aborted);
  TEST_ASSERT_NULL(listener.arg); TEST_ASSERT_NULL(listener.accept);
}
void raw_port_exposes_shared_samples_without_channel_acquisition() {
  Loopback backend; Channel channel(backend); MemorySamples memory;
  memory.observe(10000,3000); TcpSessionServer server(channel,&memory);
  tcp_pcb first,second; start(server); connect(first); negotiate(server,first,false);
  send(server,first,request("{\"op\":\"device.diagnostics\"}",9));
  TEST_ASSERT_TRUE(contains(first,"\"heap_free\":10000"));
  TEST_ASSERT_EQUAL_UINT(1,server.accepts());
  TEST_ASSERT_GREATER_THAN_UINT(0,server.receive_callbacks());
  TEST_ASSERT_GREATER_THAN_UINT(0,server.received_bytes());
  TEST_ASSERT_GREATER_THAN_UINT(0,server.sent_callbacks());
  TEST_ASSERT_GREATER_THAN_UINT(0,server.sent_bytes());
  memory.observe_transport({11,12,13,10,9,14,15,server.accepts(),
                            server.receive_callbacks(),server.sent_callbacks(),
                            server.errors(),server.received_bytes(),server.sent_bytes()});
  TEST_ASSERT_EQUAL_UINT(0,channel.owner());
  memory.observe(9999,2999); connect(second); negotiate(server,second,false);
  send(server,second,request("{\"op\":\"device.diagnostics\"}",10));
  TEST_ASSERT_TRUE(contains(second,"\"heap_min\":9999"));
  TEST_ASSERT_TRUE(contains(second,"\"samples\":2")); server.stop();
  TEST_ASSERT_TRUE(contains(second,"\"ncm_budget_exhaustions\":14"));
  TEST_ASSERT_TRUE(contains(second,"\"tcp_rx_bytes\":"));
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(raw_port_exposes_shared_samples_without_channel_acquisition);
  RUN_TEST(chained_input_partial_writes_and_memory_pressure_preserve_binary);
  RUN_TEST(coalesced_frames_stop_receive_credit_when_peer_stalls);
  RUN_TEST(graceful_close_retries_memory_failure_without_using_transferred_pcb);
  RUN_TEST(second_socket_receives_busy_without_displacing_owner);
  RUN_TEST(callbacks_defer_backend_cleanup_and_new_owner_gets_no_replay);
  RUN_TEST(fatal_json_reply_waits_for_tcp_ack_then_closes_without_dangling_callbacks);
  RUN_TEST(unacknowledged_terminal_output_and_close_memory_failure_have_deadlines);
  RUN_TEST(handshake_timeout_starts_at_accept_even_before_first_application_poll);
  RUN_TEST(transport_fault_releases_owner_and_refused_input_stays_stack_owned);
  RUN_TEST(reconnect_accept_before_application_cleanup_reuses_only_dead_socket_slot);
  RUN_TEST(accept_resource_error_with_null_pcb_never_aborts_null_or_displaces_owner);
  RUN_TEST(frequent_application_poll_dispatches_timer_owned_refused_data);
  RUN_TEST(listener_failures_are_reported_and_stop_releases_live_sessions);
  return UNITY_END();
}
