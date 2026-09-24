#pragma once
#include "core/session.h"
#include <lwip/tcp.h>

namespace firmingo {
// Raw-lwIP port. ALL calls (including destruction) require the Ethernet lock.
// Callbacks retain one pbuf chain per socket; parsing/backend work runs in poll.
// Two sockets allow an explicit channel BUSY response. Excess sockets get RST.
class TcpSessionServer {
 public:
  static constexpr unsigned connection_limit = 2;
  static constexpr uint32_t close_ms = 1000;
  using Clock = uint32_t (*)();
  explicit TcpSessionServer(Channel& channel, const MemorySamples* memory = nullptr)
      : channel_(channel), memory_(memory) {}
  ~TcpSessionServer() { stop(); }
  TcpSessionServer(const TcpSessionServer&) = delete;
  TcpSessionServer& operator=(const TcpSessionServer&) = delete;
  bool begin(const ip_addr_t* address, uint16_t port, const Identity& identity, Clock clock);
  void poll(uint32_t now);
  void stop();
  unsigned active_connections() const;
  uint32_t accepts() const { return accepts_; }
  uint32_t receive_callbacks() const { return receive_callbacks_; }
  uint64_t received_bytes() const { return received_bytes_; }
  uint32_t sent_callbacks() const { return sent_callbacks_; }
  uint64_t sent_bytes() const { return sent_bytes_; }
  uint32_t errors() const { return errors_; }
 private:
  struct Connection : ByteIO {
    TcpSessionServer* server = nullptr;
    tcp_pcb* pcb = nullptr;
    pbuf* packet = nullptr;
    uint16_t offset = 0;
    uint32_t accepted_at = 0, closing_at = 0, unacked = 0;
    bool occupied = false, closing = false, replacing = false;
    alignas(Session) unsigned char storage[sizeof(Session)];
    Session* session = nullptr;
    bool connected() const override { return pcb != nullptr && !closing; }
    std::size_t read(uint8_t*, std::size_t) override;
    std::size_t write(const uint8_t*, std::size_t) override;
    void discard_packet();
    void abort_pcb();
    void clear();
  } connections_[connection_limit];
  static err_t accept(void*, tcp_pcb*, err_t);
  static err_t receive(void*, tcp_pcb*, pbuf*, err_t);
  static void failed(void*, err_t);
  static err_t sent(void*, tcp_pcb*, uint16_t);
  static void detach(tcp_pcb*);
  static void close(Connection&, uint32_t now);
  Channel& channel_;
  const MemorySamples* const memory_;
  Identity identity_{};
  Clock clock_ = nullptr;
  tcp_pcb* listener_ = nullptr;
  uint32_t accepts_ = 0, receive_callbacks_ = 0, sent_callbacks_ = 0, errors_ = 0;
  uint64_t received_bytes_ = 0, sent_bytes_ = 0;
  static void increment(uint32_t& value) { if (value != UINT32_MAX) ++value; }
  static void add(uint64_t& value, uint64_t amount) {
    value = UINT64_MAX - value < amount ? UINT64_MAX : value + amount;
  }
};
}
