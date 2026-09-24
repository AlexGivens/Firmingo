#include "stream.h"

namespace firmingo {
constexpr std::size_t Stream::capacity;
Stream::Stream(std::uint32_t idle_timeout_ms) : idle_timeout_ms_(idle_timeout_ms) {}

StreamResult Stream::open(std::uint32_t owner, std::uint32_t now_ms) {
  if (owner == 0) return StreamResult::invalid_owner;
  if (owner_ != 0) return StreamResult::busy;
  clear();
  owner_ = owner;
  last_progress_ = now_ms;
  return StreamResult::ok;
}

void Stream::clear() {
  to_backend_.offset = to_backend_.size = 0;
  to_peer_.offset = to_peer_.size = 0;
  owner_ = 0;
}

StreamResult Stream::close(std::uint32_t owner) {
  if (owner == 0 || owner != owner_) return StreamResult::invalid_owner;
  clear();
  return StreamResult::ok;
}

bool Stream::pump(ByteIO& source, ByteIO& destination, Pending& pending,
                  bool& progress) {
  // Never read past a stalled destination. At most one read and one write
  // per direction per poll; no unbounded drain loop and no allocation.
  if (pending.size == 0) {
    const auto received = source.read(pending.bytes, capacity);
    if (received > capacity) return false;
    pending.offset = 0;
    pending.size = received;
    if (received > pending.peak) pending.peak = received;
    progress = progress || received != 0;
  }
  if (pending.size != 0) {
    const auto sent = destination.write(pending.bytes + pending.offset, pending.size);
    if (sent > pending.size) return false;
    pending.offset += sent;
    pending.size -= sent;
    progress = progress || sent != 0;
  }
  return true;
}

StreamResult Stream::poll(std::uint32_t owner, ByteIO& peer, ByteIO& backend,
                          std::uint32_t now_ms) {
  if (owner == 0 || owner != owner_) return StreamResult::invalid_owner;
  if (!peer.connected() || !backend.connected()) {
    clear();
    return StreamResult::disconnected;
  }
  // Unsigned elapsed time handles millis() rollover. Ports must poll at least
  // once per uint32 clock period. A zero timeout disables idle expiry.
  if (idle_timeout_ms_ != 0 && now_ms - last_progress_ >= idle_timeout_ms_) {
    clear();
    return StreamResult::timeout;
  }
  bool progress = false;
  if (!pump(peer, backend, to_backend_, progress) ||
      !pump(backend, peer, to_peer_, progress)) {
    clear();
    return StreamResult::io_error;
  }
  if (progress) last_progress_ = now_ms;
  return StreamResult::ok;
}
}  // namespace firmingo
