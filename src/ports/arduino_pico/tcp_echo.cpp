#include "tcp_echo.h"
#include <algorithm>

namespace firmingo {
bool TcpEcho::begin(const ip_addr_t* address, uint16_t port) {
  if (listener_) return false;
  tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
  if (!pcb) return false;
  if (tcp_bind(pcb, address, port) != ERR_OK) {
    tcp_abort(pcb);
    return false;
  }
  listener_ = tcp_listen_with_backlog(pcb, 1);
  if (!listener_) { tcp_abort(pcb); return false; }
  tcp_arg(listener_, this);
  tcp_accept(listener_, accept);
  return true;
}
void TcpEcho::clear() {
  if (received_) pbuf_free(received_);
  received_ = nullptr;
  offset_ = 0;
  stream_.close(1);
  backend_.clear();
}
void TcpEcho::abort() {
  tcp_pcb* pcb = client_;
  client_ = nullptr;
  clear();
  if (pcb) {
    tcp_arg(pcb, nullptr);
    tcp_recv(pcb, nullptr);
    tcp_err(pcb, nullptr);
    tcp_abort(pcb);
  }
}
err_t TcpEcho::accept(void* arg, tcp_pcb* pcb, err_t error) {
  auto& self = *static_cast<TcpEcho*>(arg);
  if (error != ERR_OK || self.client_) {
    // Legacy raw endpoint cannot encode BUSY without corrupting payload.
    // Refuse the newcomer with RST; never displace the current owner.
    tcp_abort(pcb);
    return ERR_ABRT;
  }
  self.clear();
  self.client_ = pcb;
  tcp_arg(pcb, &self);
  tcp_recv(pcb, receive);
  tcp_err(pcb, failed);
  tcp_nagle_disable(pcb);
  return ERR_OK;
}
err_t TcpEcho::receive(void* arg, tcp_pcb*, pbuf* packet, err_t error) {
  auto& self = *static_cast<TcpEcho*>(arg);
  if (!packet || error != ERR_OK) {
    if (packet) pbuf_free(packet);
    self.abort();  // EOF discards queued data; no half-close contract yet.
    return ERR_ABRT;
  }
  if (self.received_) return ERR_MEM; // lwIP retains ownership and retries.
  self.received_ = packet;
  return ERR_OK;
}
void TcpEcho::failed(void* arg, err_t) {
  auto& self = *static_cast<TcpEcho*>(arg);
  self.client_ = nullptr; // lwIP already freed the PCB.
  self.clear();
}
std::size_t TcpEcho::read(uint8_t* data, std::size_t capacity) {
  if (!client_ || !received_) return 0;
  const auto count = static_cast<uint16_t>(std::min<std::size_t>(
      capacity, received_->tot_len - offset_));
  const auto copied = pbuf_copy_partial(received_, data, count, offset_);
  offset_ += copied;
  tcp_recved(client_, copied);
  if (offset_ == received_->tot_len) {
    pbuf_free(received_);
    received_ = nullptr;
    offset_ = 0;
  }
  return copied;
}
std::size_t TcpEcho::write(const uint8_t* data, std::size_t size) {
  if (!client_) return 0;
  const auto count = static_cast<uint16_t>(std::min<std::size_t>(size, tcp_sndbuf(client_)));
  if (!count) return 0;
  const err_t result = tcp_write(client_, data, count, TCP_WRITE_FLAG_COPY);
  if (result == ERR_MEM) return 0; // No wait, no lost tail; Stream retries later.
  if (result != ERR_OK) { abort(); return 0; }
  return count;
}
void TcpEcho::poll(uint32_t now_ms) {
  if (!client_) return;
  if (!stream_.owner()) stream_.open(1, now_ms);
  if (stream_.poll(1, *this, backend_, now_ms) != StreamResult::ok) {
    abort();
    return;
  }
  // At most one output attempt per application turn, including retry after
  // a transient link error. TCP retains copied data until acknowledged.
  if (client_) tcp_output(client_);
}
}
