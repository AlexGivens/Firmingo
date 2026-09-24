#include "ports/arduino_pico/tcp_echo.h"
#include "unity.h"
#include <array>
#include <cstring>
#include <vector>
static tcp_pcb listener;
static unsigned freed, credited, writes, outputs;
static err_t write_result;
static std::vector<uint8_t> returned;
tcp_pcb* tcp_new_ip_type(int) { return &listener; }
err_t tcp_bind(tcp_pcb*, const ip_addr_t*, uint16_t) { return ERR_OK; }
tcp_pcb* tcp_listen_with_backlog(tcp_pcb* p, int) { return p; }
void tcp_abort(tcp_pcb* p) { p->aborted=true; }
void tcp_recved(tcp_pcb*, uint16_t n) { credited+=n; }
err_t tcp_write(tcp_pcb*, const void* data, uint16_t n, int flags) {
  ++writes;
  TEST_ASSERT_EQUAL_INT(TCP_WRITE_FLAG_COPY, flags);
  if (write_result != ERR_OK) return write_result;
  const auto* bytes = static_cast<const uint8_t*>(data);
  returned.insert(returned.end(), bytes, bytes+n);
  return ERR_OK;
}
err_t tcp_output(tcp_pcb*) { ++outputs; return ERR_OK; }
void pbuf_free(pbuf*) { ++freed; }
uint16_t pbuf_copy_partial(pbuf* p, void* data, uint16_t n, uint16_t offset) {
  TEST_ASSERT_TRUE(offset+n <= p->tot_len);
  std::memcpy(data,p->payload+offset,n); return n;
}
void setUp() {
  listener = tcp_pcb{};
  freed=credited=writes=outputs=0;
  write_result=ERR_OK;
  returned.clear();
}
void tearDown() {}
static void open(firmingo::TcpEcho& echo, tcp_pcb& peer) {
  TEST_ASSERT_TRUE(echo.begin(nullptr,5001));
  TEST_ASSERT_EQUAL_INT(ERR_OK,listener.accept(listener.arg,&peer,ERR_OK));
}
void full_echo_survives_send_memory_exhaustion_and_partial_writes() {
  firmingo::TcpEcho echo;
  tcp_pcb peer;
  open(echo,peer);
  std::array<uint8_t,4096> data{};
  for(unsigned i=0;i<data.size();++i) data[i]=(i*73)^(i>>8);
  pbuf packet{static_cast<uint16_t>(data.size()),data.data()};
  TEST_ASSERT_EQUAL_INT(ERR_OK,peer.recv(peer.arg,&peer,&packet,ERR_OK));
  write_result=ERR_MEM;
  for(unsigned i=0;i<100;++i) echo.poll(i);
  TEST_ASSERT_EQUAL_UINT(0,returned.size());
  // Core + backend absorb at most three batches; the receive chain remains owned.
  TEST_ASSERT_TRUE(credited<=768);
  TEST_ASSERT_EQUAL_UINT(0,freed);
  TEST_ASSERT_TRUE(writes<=100);
  write_result=ERR_OK;
  peer.room=7;
  for(unsigned i=100;i<2000 && returned.size()<data.size();++i) echo.poll(i);
  TEST_ASSERT_EQUAL_UINT(data.size(),returned.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(data.data(),returned.data(),data.size());
  TEST_ASSERT_EQUAL_UINT(data.size(),credited);
  TEST_ASSERT_EQUAL_UINT(1,freed);
}
void refused_packet_remains_owned_by_stack_and_busy_peer_cannot_steal() {
  firmingo::TcpEcho echo;
  tcp_pcb peer, other;
  open(echo,peer);
  uint8_t bytes[2]={0,255};
  pbuf first{2,bytes},second{2,bytes};
  TEST_ASSERT_EQUAL_INT(ERR_OK,peer.recv(peer.arg,&peer,&first,ERR_OK));
  TEST_ASSERT_EQUAL_INT(ERR_MEM,peer.recv(peer.arg,&peer,&second,ERR_OK));
  TEST_ASSERT_EQUAL_UINT(0,freed);
  TEST_ASSERT_EQUAL_INT(ERR_ABRT,listener.accept(listener.arg,&other,ERR_OK));
  TEST_ASSERT_TRUE(other.aborted);
  TEST_ASSERT_FALSE(peer.aborted);
  echo.poll(0);
  TEST_ASSERT_EQUAL_UINT(1,freed);
  TEST_ASSERT_EQUAL_INT(ERR_OK,peer.recv(peer.arg,&peer,&second,ERR_OK));
  echo.poll(1);
  TEST_ASSERT_EQUAL_UINT(4,returned.size());
  TEST_ASSERT_EQUAL_UINT(2,freed);
}
void eof_and_error_clear_pending_data_before_reconnect() {
  for(bool eof : {false,true}) {
    firmingo::TcpEcho echo;
    tcp_pcb peer, next;
    open(echo,peer);
    uint8_t bytes[1024]={42};
    pbuf packet{1024,bytes};
    peer.recv(peer.arg,&peer,&packet,ERR_OK);
    peer.room=0;
    echo.poll(0);
    if(eof) TEST_ASSERT_EQUAL_INT(ERR_ABRT,peer.recv(peer.arg,&peer,nullptr,ERR_OK));
    else peer.error(peer.arg,ERR_CONN);
    TEST_ASSERT_FALSE(echo.connected());
    TEST_ASSERT_EQUAL_INT(ERR_OK,listener.accept(listener.arg,&next,ERR_OK));
    echo.poll(1);
    TEST_ASSERT_EQUAL_UINT(0,returned.size());
  }
}
void idle_connection_expires_without_waiting_for_peer() {
  firmingo::TcpEcho echo;
  tcp_pcb peer;
  open(echo,peer);
  echo.poll(10);
  echo.poll(30010);
  TEST_ASSERT_TRUE(peer.aborted);
  TEST_ASSERT_FALSE(echo.connected());
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(full_echo_survives_send_memory_exhaustion_and_partial_writes);
  RUN_TEST(refused_packet_remains_owned_by_stack_and_busy_peer_cannot_steal);
  RUN_TEST(eof_and_error_clear_pending_data_before_reconnect);
  RUN_TEST(idle_connection_expires_without_waiting_for_peer);
  return UNITY_END();
}
