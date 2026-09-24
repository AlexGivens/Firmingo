#include "tcp_session.h"
#include <algorithm>
#include <new>
#include <lwip/timeouts.h>

namespace firmingo {
bool TcpSessionServer::begin(const ip_addr_t* address, uint16_t port,
                             const Identity& identity, Clock clock) {
  if (listener_ || !clock) return false;
  tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
  if (!pcb) return false;
  if (tcp_bind(pcb, address, port) != ERR_OK) { tcp_abort(pcb); return false; }
  listener_ = tcp_listen_with_backlog(pcb, connection_limit);
  if (!listener_) { tcp_abort(pcb); return false; }
  identity_ = identity;
  clock_ = clock;
  tcp_arg(listener_, this);
  tcp_accept(listener_, accept);
  return true;
}
void TcpSessionServer::detach(tcp_pcb* pcb) {
  tcp_arg(pcb, nullptr);
  tcp_recv(pcb, nullptr);
  tcp_err(pcb, nullptr);
  tcp_sent(pcb, nullptr);
}
void TcpSessionServer::Connection::discard_packet() {
  if (packet) pbuf_free(packet);
  packet = nullptr;
  offset = 0;
}
void TcpSessionServer::Connection::abort_pcb() {
  tcp_pcb* old = pcb;
  pcb = nullptr;
  if (old) { detach(old); tcp_abort(old); }
}
void TcpSessionServer::Connection::clear() {
  if (session) { session->~Session(); session = nullptr; }
  abort_pcb();
  discard_packet();
  occupied = closing = replacing = false;
  unacked = 0;
}
void TcpSessionServer::stop() {
  for (auto& connection : connections_) connection.clear();
  if (listener_) {
    tcp_arg(listener_, nullptr);
    tcp_accept(listener_, nullptr);
    tcp_close(listener_); // LISTEN close frees immediately in the pinned lwIP.
    listener_ = nullptr;
  }
}
unsigned TcpSessionServer::active_connections() const {
  unsigned count = 0;
  for (const auto& connection : connections_) count += connection.occupied;
  return count;
}
err_t TcpSessionServer::accept(void* arg, tcp_pcb* pcb, err_t error) {
  auto& self = *static_cast<TcpSessionServer*>(arg);
  // The pinned lwIP reports listen allocation exhaustion with a NULL PCB.
  if (!pcb) {
    if (error == ERR_OK) return ERR_MEM;
    return error;
  }
  if (error == ERR_OK) for (auto& connection : self.connections_) {
    if (connection.occupied && connection.pcb) continue;
    // EOF/error may precede a new accept in the SAME Ethernet worker turn.
    // Reserve the dead socket's slot now, but leave its Session/backend/pbuf
    // cleanup to poll before constructing the replacement. No third live PCB.
    connection.replacing = connection.occupied;
    connection.occupied = true;
    connection.server = &self;
    connection.pcb = pcb;
    connection.accepted_at = self.clock_();
    tcp_arg(pcb, &connection);
    tcp_recv(pcb, receive);
    tcp_err(pcb, failed);
    tcp_sent(pcb, sent);
    tcp_nagle_disable(pcb);
    increment(self.accepts_);
    return ERR_OK;
  }
  tcp_abort(pcb);
  return ERR_ABRT;
}
err_t TcpSessionServer::receive(void* arg, tcp_pcb*, pbuf* packet, err_t error) {
  auto& connection = *static_cast<Connection*>(arg);
  if (!packet || error != ERR_OK) {
    if (packet) pbuf_free(packet);
    // EOF is a disconnect, not a supported half-close. Backend/session disposal
    // is deferred until poll, so callbacks never execute application work.
    connection.abort_pcb();
    return ERR_ABRT;
  }
  if (connection.packet || connection.closing || connection.replacing) return ERR_MEM; // Stack owns it.
  connection.packet = packet;
  if (connection.server) {
    increment(connection.server->receive_callbacks_);
    add(connection.server->received_bytes_,packet->tot_len);
  }
  return ERR_OK;
}
void TcpSessionServer::failed(void* arg, err_t) {
  // lwIP already destroyed the PCB. Keep owned pbuf/session alive until poll.
  auto& connection = *static_cast<Connection*>(arg);
  if (connection.server) increment(connection.server->errors_);
  connection.pcb = nullptr;
}
err_t TcpSessionServer::sent(void* arg, tcp_pcb*, uint16_t bytes) {
  auto& connection = *static_cast<Connection*>(arg);
  if (connection.server) {
    increment(connection.server->sent_callbacks_);
    add(connection.server->sent_bytes_,bytes);
  }
  connection.unacked -= std::min<uint32_t>(bytes, connection.unacked);
  return ERR_OK;
}
std::size_t TcpSessionServer::Connection::read(uint8_t* data, std::size_t capacity) {
  if (!connected() || !packet) return 0;
  const auto count = static_cast<uint16_t>(std::min<std::size_t>(capacity, packet->tot_len - offset));
  const auto copied = pbuf_copy_partial(packet, data, count, offset);
  offset += copied;
  tcp_recved(pcb, copied); // Credit only bytes handed to the production decoder.
  if (offset == packet->tot_len) discard_packet();
  return copied;
}
std::size_t TcpSessionServer::Connection::write(const uint8_t* data, std::size_t size) {
  if (!connected()) return 0;
  const auto count = static_cast<uint16_t>(std::min<std::size_t>(size, tcp_sndbuf(pcb)));
  if (!count) return 0;
  const err_t error = tcp_write(pcb, data, count, TCP_WRITE_FLAG_COPY);
  if (error == ERR_MEM) return 0;
  if (error != ERR_OK) { abort_pcb(); return 0; }
  unacked += count; // Bounded by the pinned TCP send buffer, not peer progress.
  return count;
}
void TcpSessionServer::close(Connection& connection, uint32_t now) {
  if (!connection.pcb) { connection.clear(); return; }
  // Do not abort just after copying a fatal JSON error: give TCP one bounded
  // second to acknowledge already queued output. This is NOT an application ack.
  if (static_cast<uint32_t>(now - connection.closing_at) >= close_ms) {
    connection.clear();
    return;
  }
  if (connection.unacked) { tcp_output(connection.pcb); return; }
  // Remaining input is discarded. lwIP may issue RST for its own refused data;
  // all emitted output is acknowledged first, so that cannot erase the reply.
  if (connection.packet) tcp_recved(connection.pcb, connection.packet->tot_len - connection.offset);
  connection.discard_packet();
  tcp_pcb* pcb = connection.pcb;
  detach(pcb);
  const err_t error = tcp_close(pcb);
  if (error == ERR_OK) {
    connection.pcb = nullptr; // Success transfers lifetime to lwIP; never reuse.
    connection.clear();
  } else if (error == ERR_MEM) {
    tcp_arg(pcb, &connection);
    tcp_recv(pcb, receive);
    tcp_err(pcb, failed);
    tcp_sent(pcb, sent);
  } else {
    connection.clear();
  }
}
void TcpSessionServer::poll(uint32_t now) {
  // Arduino-Pico's Ethernet worker reschedules its 20 ms background timeout
  // whenever the application releases the lwIP lock. A frequent raw loop must
  // dispatch timers explicitly or refused RX/retransmission can starve forever.
  sys_check_timeouts();
  // Dispose all disconnected owners before other sockets can acquire a channel.
  for (auto& connection : connections_) {
    if (connection.replacing) {
      if (connection.session) { connection.session->~Session(); connection.session = nullptr; }
      connection.discard_packet(); // Old socket's bytes: never credit the new PCB.
      connection.closing = connection.replacing = false;
      connection.unacked = 0;
    }
    if (connection.occupied && !connection.pcb) connection.clear();
  }
  for (unsigned i = 0; i < connection_limit; ++i) {
    auto& connection = connections_[i];
    if (!connection.occupied) continue;
    if (connection.closing) { close(connection, now); continue; }
    if (!connection.session) connection.session = new (connection.storage)
      Session(channel_, identity_, i + 1, connection.accepted_at, memory_);
    const auto result = connection.session->poll(connection, now);
    if (!connection.pcb) { connection.clear(); continue; }
    if (result != SessionResult::running) {
      connection.session->~Session();
      connection.session = nullptr;
      connection.closing = true;
      connection.closing_at = now;
      close(connection, now);
    } else {
      tcp_output(connection.pcb); // One bounded attempt; TCP owns copied bytes.
    }
  }
}
}
