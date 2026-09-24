#pragma once
#include "protocol.h"
#include "stream.h"
#include "diagnostics.h"

namespace firmingo {
struct Identity {
  char device_id[17], boot_id[17], board_id[33], firmware_version[33];
};
// One application backend/channel shared by concurrent sessions. Calls must be
// serialized; the backend outlives the channel, which outlives its sessions.
class Channel : private ByteIO {
 public:
  explicit Channel(StreamBackend& backend) : backend_(backend), stream_(0) {}
  Channel(const Channel&) = delete;
  Channel& operator=(const Channel&) = delete;
  StreamResult acquire(uint32_t owner, uint32_t now,
                       const SerialConfiguration* configuration = nullptr);
  BackendResult configure(uint32_t owner, const SerialConfiguration& configuration);
  void release(uint32_t owner);
  StreamResult poll(uint32_t owner, ByteIO& peer, uint32_t now);
  uint32_t owner() const { return stream_.owner(); }
  std::size_t pending_rx() const { return stream_.pending_to_backend(); }
  std::size_t pending_tx() const { return stream_.pending_to_peer(); }
  std::size_t peak_rx() const { return stream_.peak_to_backend(); }
  std::size_t peak_tx() const { return stream_.peak_to_peer(); }
  bool backend_diagnostics(BackendDiagnostics& value) const {
    return backend_.diagnostics(value);
  }
  BackendKind backend_kind() const { return backend_.kind(); }
  bool configuration(SerialConfiguration& value) const {
    return backend_.configuration(value);
  }
  uint64_t rx_bytes() const { return rx_bytes_; }
  uint64_t tx_bytes() const { return tx_bytes_; }
 private:
  bool connected() const override { return backend_.connected(); }
  std::size_t read(uint8_t* data, std::size_t capacity) override;
  std::size_t write(const uint8_t* data, std::size_t size) override;
  StreamBackend& backend_;
  Stream stream_;
  uint64_t rx_bytes_ = 0, tx_bytes_ = 0;
};
enum class SessionResult { running, closed, disconnected, timeout, protocol_error, io_error };
class Session : private ByteIO {
 public:
  static constexpr uint32_t handshake_ms = 5000, frame_ms = 5000, idle_ms = 30000, error_flush_ms = 1000;
  static constexpr std::size_t response_capacity = 800;
  // Port supplies unique nonzero tokens per concurrent socket and monotonic ms.
  Session(Channel& channel, const Identity& identity, uint32_t owner, uint32_t now, const MemorySamples* memory = nullptr);
  ~Session();
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  SessionResult poll(ByteIO& transport, uint32_t now);
  void close();
  bool negotiated() const { return negotiated_; }
  bool owns_channel() const { return owned_; }
  std::size_t limit() const { return decoder_.limit(); }
 private:
  bool connected() const override { return owned_; }
  std::size_t read(uint8_t* data, std::size_t capacity) override;
  std::size_t write(const uint8_t* data, std::size_t size) override;
  void queue(protocol::Type type, uint32_t request, const uint8_t* payload, std::size_t size);
  void error(uint32_t request, const char* code);
  void success(uint32_t request);
  void dispatch(uint32_t now);
  SessionResult pump(uint32_t now);
  SessionResult finish(SessionResult result);
  Channel& channel_;
  const MemorySamples* const memory_;
  const Identity identity_;
  const uint32_t owner_, started_at_;
  uint32_t progress_at_, frame_at_ = 0, closing_at_ = 0;
  protocol::Decoder decoder_;
  uint8_t output_[protocol::header_size + response_capacity]{};
  std::size_t output_offset_ = 0, output_size_ = 0, data_offset_ = 0;
  bool negotiated_ = false, owned_ = false, closing_ = false, data_turn_ = false;
  SessionResult result_ = SessionResult::running;
};
}  // namespace firmingo
