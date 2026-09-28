#include "upload_server.h"
#include "unity.h"

#include <algorithm>
#include <cstring>
#include <vector>

using namespace firmingo_managed;
namespace {
constexpr std::uint32_t api = 0x1002154c;
constexpr char id[] = "5031503337360009";
tcp_pcb listener;
std::vector<std::uint8_t> outbound;
unsigned credited = 0, freed = 0, closes = 0;
std::vector<std::uint8_t> capsule() {
  std::vector<std::uint8_t> image(kCodeOffset + 8);
  std::memcpy(image.data(), "FMS1", 4);
  image[4] = kAbiVersion;
  image[6] = kHeaderBytes;
  const auto put32 = [&](std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) image[at + i] = value >> (8 * i);
  };
  put32(8, kNanoBoardTag); put32(12, api);
  put32(16, kSlotAddress + kCodeOffset); put32(20, 8);
  put32(24, kSlotAddress + kCodeOffset + 1);
  put32(28, kSlotAddress + kCodeOffset + 5);
  for (unsigned i = 0; i < 8; ++i) image[kCodeOffset + i] = i * 11;
  sha256(image.data() + kCodeOffset, 8, image.data() + 32);
  return image;
}
std::vector<std::uint8_t> frame() {
  const auto image = capsule();
  std::vector<std::uint8_t> bytes(kUploadHeaderBytes + image.size() + 4);
  std::memcpy(bytes.data(), "FMSU", 4); bytes[4] = 1;
  for (unsigned i = 0; i < 4; ++i) bytes[8 + i] = image.size() >> (8 * i);
  sha256(image.data(), image.size(), bytes.data() + 12);
  std::memcpy(bytes.data() + 44, id, 16);
  std::memcpy(bytes.data() + kUploadHeaderBytes, image.data(), image.size());
  std::memcpy(bytes.data() + kUploadHeaderBytes + image.size(), "GO!!", 4);
  return bytes;
}
void connect(tcp_pcb& peer) {
  TEST_ASSERT_EQUAL_INT(ERR_OK, listener.accept(listener.arg, &peer, ERR_OK));
}
void send(ProofUploadServer& server, tcp_pcb& peer,
          std::vector<std::uint8_t>& bytes, bool armed, std::uint32_t now) {
  pbuf packet{static_cast<std::uint16_t>(bytes.size()), bytes.data()};
  TEST_ASSERT_EQUAL_INT(ERR_OK, peer.recv(peer.arg, &peer, &packet, ERR_OK));
  server.poll(armed, now);
  TEST_ASSERT_EQUAL_UINT(bytes.size(), credited);
  TEST_ASSERT_EQUAL_UINT(1, freed);
}
}  // namespace

tcp_pcb* tcp_new_ip_type(int) { return &listener; }
err_t tcp_bind(tcp_pcb*, const ip_addr_t*, std::uint16_t port) {
  TEST_ASSERT_EQUAL_UINT(7421, port); return ERR_OK;
}
tcp_pcb* tcp_listen_with_backlog(tcp_pcb* pcb, int backlog) {
  TEST_ASSERT_EQUAL_INT(1, backlog); return pcb;
}
err_t tcp_close(tcp_pcb*) { ++closes; return ERR_OK; }
void tcp_abort(tcp_pcb* pcb) { pcb->aborted = true; }
void tcp_recved(tcp_pcb*, std::uint16_t count) { credited += count; }
err_t tcp_write(tcp_pcb*, const void* data, std::uint16_t count, int flags) {
  TEST_ASSERT_EQUAL_INT(TCP_WRITE_FLAG_COPY, flags);
  const auto* p = static_cast<const std::uint8_t*>(data);
  outbound.insert(outbound.end(), p, p + count);
  return ERR_OK;
}
err_t tcp_output(tcp_pcb*) { return ERR_OK; }
void pbuf_free(pbuf*) { ++freed; }
std::uint16_t pbuf_copy_partial(pbuf* packet, void* out,
                                std::uint16_t count, std::uint16_t offset) {
  if (offset + count > packet->tot_len) return 0;
  std::memcpy(out, packet->payload + offset, count);
  return count;
}
void setUp() {
  listener = tcp_pcb{};
  outbound.clear(); credited = freed = closes = 0;
}
void tearDown() {}

void unarmed_upload_is_denied_without_staging() {
  CapsuleStage stage;
  ProofUploadServer server(stage, api, id);
  ip_addr_t address;
  TEST_ASSERT_TRUE(server.begin(&address, 7421));
  tcp_pcb peer; connect(peer);
  auto bytes = frame();
  // The receiver refuses at the header, so the rest of the pbuf is discarded.
  pbuf packet{static_cast<std::uint16_t>(bytes.size()), bytes.data()};
  TEST_ASSERT_EQUAL_INT(ERR_OK, peer.recv(peer.arg, &peer, &packet, ERR_OK));
  server.poll(false, 1);
  TEST_ASSERT_EQUAL_UINT8('D', outbound.at(0));
  TEST_ASSERT_EQUAL_UINT(0, server.take_commit());
  TEST_ASSERT_FALSE(stage.active());
  TEST_ASSERT_EQUAL_UINT(1, freed);
}

void armed_upload_stages_then_reports_readback_completion() {
  CapsuleStage stage;
  ProofUploadServer server(stage, api, id);
  ip_addr_t address;
  TEST_ASSERT_TRUE(server.begin(&address, 7421));
  tcp_pcb peer; connect(peer);
  auto bytes = frame();
  send(server, peer, bytes, true, 1);
  TEST_ASSERT_EQUAL_UINT(0, server.take_commit());
  TEST_ASSERT_EQUAL_INT(ERR_OK, peer.recv(peer.arg, &peer, nullptr, ERR_OK));
  server.poll(true, 2);
  const auto owner = server.take_commit();
  TEST_ASSERT_TRUE(owner != 0);
  std::size_t size = 0;
  TEST_ASSERT_NOT_NULL(stage.verified(owner, &size));
  TEST_ASSERT_EQUAL_UINT(capsule().size(), size);
  server.finish_commit(owner, {SwitchStatus::complete, InstallResult::ok, true});
  server.poll(true, 3);
  TEST_ASSERT_EQUAL_UINT8('K', outbound.at(0));
  TEST_ASSERT_NOT_NULL(peer.sent);
  TEST_ASSERT_EQUAL_INT(ERR_OK, peer.sent(peer.arg, &peer, 1));
  server.poll(true, 4);
  TEST_ASSERT_EQUAL_UINT(1, closes);
  TEST_ASSERT_NULL(peer.arg);
}

void malformed_frame_and_disconnect_drop_stage() {
  CapsuleStage stage;
  ProofUploadServer server(stage, api, id);
  ip_addr_t address;
  TEST_ASSERT_TRUE(server.begin(&address, 7421));
  tcp_pcb peer; connect(peer);
  auto bytes = frame();
  bytes.back() ^= 1;
  pbuf bad{static_cast<std::uint16_t>(bytes.size()), bytes.data()};
  TEST_ASSERT_EQUAL_INT(ERR_OK, peer.recv(peer.arg, &peer, &bad, ERR_OK));
  server.poll(true, 1);
  TEST_ASSERT_EQUAL_UINT8('I', outbound.at(0));
  TEST_ASSERT_FALSE(stage.active());
  TEST_ASSERT_EQUAL_UINT(0, server.take_commit());
  TEST_ASSERT_EQUAL_INT(ERR_OK, peer.recv(peer.arg, &peer, nullptr, ERR_OK));
  server.poll(true, 2);
  TEST_ASSERT_FALSE(stage.active());
}

void blocked_response_socket_is_bounded_and_closed() {
  CapsuleStage stage;
  ProofUploadServer server(stage, api, id);
  ip_addr_t address;
  TEST_ASSERT_TRUE(server.begin(&address, 7421));
  tcp_pcb peer; peer.room = 0; connect(peer);
  auto bytes = frame();
  pbuf packet{static_cast<std::uint16_t>(bytes.size()), bytes.data()};
  TEST_ASSERT_EQUAL_INT(ERR_OK, peer.recv(peer.arg, &peer, &packet, ERR_OK));
  server.poll(false, 1);
  TEST_ASSERT_TRUE(outbound.empty());
  server.poll(false, 2001);
  TEST_ASSERT_TRUE(peer.aborted);
  TEST_ASSERT_FALSE(stage.active());
}

void trailing_packet_and_truncated_half_close_refuse_before_commit() {
  {
    CapsuleStage stage;
    ProofUploadServer server(stage, api, id);
    ip_addr_t address;
    TEST_ASSERT_TRUE(server.begin(&address, 7421));
    tcp_pcb peer; connect(peer);
    auto bytes = frame();
    send(server, peer, bytes, true, 1);
    std::uint8_t extra = 0;
    pbuf packet{1, &extra};
    TEST_ASSERT_EQUAL_INT(ERR_OK, peer.recv(peer.arg, &peer, &packet, ERR_OK));
    server.poll(true, 2);
    TEST_ASSERT_EQUAL_UINT8('I', outbound.at(0));
    TEST_ASSERT_EQUAL_UINT(0, server.take_commit());
    TEST_ASSERT_FALSE(stage.active());
  }
  setUp();
  {
    CapsuleStage stage;
    ProofUploadServer server(stage, api, id);
    ip_addr_t address;
    TEST_ASSERT_TRUE(server.begin(&address, 7421));
    tcp_pcb peer; connect(peer);
    auto bytes = frame();
    pbuf packet{16, bytes.data()};
    TEST_ASSERT_EQUAL_INT(ERR_OK, peer.recv(peer.arg, &peer, &packet, ERR_OK));
    server.poll(true, 1);
    TEST_ASSERT_EQUAL_INT(ERR_OK, peer.recv(peer.arg, &peer, nullptr, ERR_OK));
    server.poll(true, 2);
    TEST_ASSERT_EQUAL_UINT8('I', outbound.at(0));
    TEST_ASSERT_EQUAL_UINT(0, server.take_commit());
    TEST_ASSERT_FALSE(stage.active());
  }
}

void complete_frame_without_half_close_times_out_without_commit() {
  CapsuleStage stage;
  ProofUploadServer server(stage, api, id);
  ip_addr_t address;
  TEST_ASSERT_TRUE(server.begin(&address, 7421));
  tcp_pcb peer; connect(peer);
  auto bytes = frame();
  send(server, peer, bytes, true, 1);
  TEST_ASSERT_TRUE(stage.active());
  TEST_ASSERT_EQUAL_UINT(0, server.take_commit());
  server.poll(true, 5001);
  TEST_ASSERT_EQUAL_UINT8('T', outbound.at(0));
  TEST_ASSERT_EQUAL_UINT(0, server.take_commit());
  TEST_ASSERT_FALSE(stage.active());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(unarmed_upload_is_denied_without_staging);
  RUN_TEST(armed_upload_stages_then_reports_readback_completion);
  RUN_TEST(malformed_frame_and_disconnect_drop_stage);
  RUN_TEST(blocked_response_socket_is_bounded_and_closed);
  RUN_TEST(trailing_packet_and_truncated_half_close_refuse_before_commit);
  RUN_TEST(complete_frame_without_half_close_times_out_without_commit);
  return UNITY_END();
}
