#pragma once
#include "core/stream.h"
#include "core/loopback.h"
#include <lwip/tcp.h>

namespace firmingo {
// Qualification echo on the legacy endpoint. All calls, including begin/poll,
// require the Ethernet lwIP lock. Callbacks execute under that same lock.
class TcpEcho : public ByteIO {
 public:
  bool begin(const ip_addr_t* address, uint16_t port);
  void poll(uint32_t now_ms);
  bool connected() const override { return client_ != nullptr; }
  std::size_t read(uint8_t* data, std::size_t capacity) override;
  std::size_t write(const uint8_t* data, std::size_t size) override;
 private:
  static err_t accept(void* arg, tcp_pcb* pcb, err_t error);
  static err_t receive(void* arg, tcp_pcb* pcb, pbuf* packet, err_t error);
  static void failed(void* arg, err_t error);
  void clear();
  void abort();
  tcp_pcb* listener_ = nullptr;
  tcp_pcb* client_ = nullptr;
  // One owned pbuf chain; lwIP bounds receive memory by TCP_WND/pool limits.
  // Further packets stay in lwIP's refused-data path until this is consumed.
  pbuf* received_ = nullptr;
  uint16_t offset_ = 0;
  Stream stream_;
  Loopback backend_;
};
}
