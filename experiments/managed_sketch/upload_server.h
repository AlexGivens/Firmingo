#pragma once

#include "upload_wire.h"
#include "switch.h"

#include <lwip/tcp.h>

namespace firmingo_managed {

// One private Nano proof socket. All methods and callbacks run under the
// Ethernet/lwIP lock. It stages only; flash runs later outside that lock.
class ProofUploadServer {
 public:
  ProofUploadServer(CapsuleStage& stage, std::uint32_t api_address,
                    const char* device_id)
      : stage_(stage), api_address_(api_address), device_id_(device_id) {}
  bool begin(const ip_addr_t* address, std::uint16_t port);
  void poll(bool physically_armed, std::uint32_t now);
  std::uint32_t take_commit();
  void cancel_commit(std::uint32_t owner, char response);
  void finish_commit(std::uint32_t owner, SwitchReport report);

 private:
  static err_t accept(void* arg, tcp_pcb* pcb, err_t error);
  static err_t receive(void* arg, tcp_pcb* pcb, pbuf* packet, err_t error);
  static void failed(void* arg, err_t error);
  static err_t sent(void* arg, tcp_pcb* pcb, std::uint16_t bytes);
  void detach(tcp_pcb* pcb);
  void discard_packet();
  void clear();
  void refuse(char response);
  void queue_response(char response);
  CapsuleStage& stage_;
  const std::uint32_t api_address_;
  const char* const device_id_;
  tcp_pcb* listener_ = nullptr;
  tcp_pcb* pcb_ = nullptr;
  pbuf* packet_ = nullptr;
  std::uint16_t offset_ = 0;
  std::uint32_t next_owner_ = 1, owner_ = 0, accepted_at_ = 0;
  std::uint32_t response_at_ = 0, last_poll_at_ = 0;
  alignas(UploadReceiver) unsigned char receiver_storage_[sizeof(UploadReceiver)];
  UploadReceiver* receiver_ = nullptr;
  char response_ = 0;
  bool occupied_ = false, commit_pending_ = false, commit_running_ = false;
  bool input_ended_ = false;
  bool response_written_ = false, response_acked_ = false;
};

}  // namespace firmingo_managed
