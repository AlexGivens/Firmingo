#include "upload_server.h"

#include <new>

namespace firmingo_managed {

bool ProofUploadServer::begin(const ip_addr_t* address, std::uint16_t port) {
  if (listener_ || !address) return false;
  tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
  if (!pcb) return false;
  if (tcp_bind(pcb, address, port) != ERR_OK) {
    tcp_abort(pcb);
    return false;
  }
  listener_ = tcp_listen_with_backlog(pcb, 1);
  if (!listener_) {
    tcp_abort(pcb);
    return false;
  }
  tcp_arg(listener_, this);
  tcp_accept(listener_, accept);
  return true;
}

void ProofUploadServer::detach(tcp_pcb* pcb) {
  tcp_arg(pcb, nullptr);
  tcp_recv(pcb, nullptr);
  tcp_sent(pcb, nullptr);
  tcp_err(pcb, nullptr);
}

void ProofUploadServer::discard_packet() {
  if (packet_) pbuf_free(packet_);
  packet_ = nullptr;
  offset_ = 0;
}

void ProofUploadServer::clear() {
  if (receiver_) {
    if (!commit_running_) receiver_->abort();
    receiver_->~UploadReceiver();
    receiver_ = nullptr;
  }
  discard_packet();
  if (pcb_) {
    tcp_pcb* old = pcb_;
    pcb_ = nullptr;
    detach(old);
    tcp_abort(old);
  }
  occupied_ = commit_pending_ = commit_running_ = input_ended_ = false;
  response_written_ = response_acked_ = false;
  response_ = 0;
  owner_ = 0;
}

err_t ProofUploadServer::accept(void* arg, tcp_pcb* pcb, err_t error) {
  auto& self = *static_cast<ProofUploadServer*>(arg);
  if (!pcb) {
    if (error == ERR_OK) return ERR_MEM;
    return error;
  }
  if (error != ERR_OK || self.occupied_) {
    tcp_abort(pcb);
    return ERR_ABRT;
  }
  self.occupied_ = true;
  self.pcb_ = pcb;
  self.owner_ = self.next_owner_++;
  if (!self.next_owner_) self.next_owner_ = 1;
  self.accepted_at_ = 0;  // Set by the next core-0 poll.
  self.input_ended_ = false;
  self.receiver_ = new (self.receiver_storage_)
      UploadReceiver(self.stage_, self.owner_, self.api_address_, self.device_id_);
  tcp_arg(pcb, &self);
  tcp_recv(pcb, receive);
  tcp_err(pcb, failed);
  tcp_sent(pcb, sent);
  tcp_nagle_disable(pcb);
  return ERR_OK;
}

err_t ProofUploadServer::receive(void* arg, tcp_pcb*, pbuf* packet, err_t error) {
  auto& self = *static_cast<ProofUploadServer*>(arg);
  if (!packet && error == ERR_OK) {
    // TCP half-close is the end marker: only then can we rule out trailing
    // bytes before touching flash. The reply still travels on this socket.
    self.input_ended_ = true;
    return ERR_OK;
  }
  if (error != ERR_OK) {
    if (packet) pbuf_free(packet);
    tcp_pcb* old = self.pcb_;
    self.pcb_ = nullptr;
    if (old) {
      self.detach(old);
      tcp_abort(old);
    }
    return ERR_ABRT;
  }
  if (self.packet_ || self.response_ || self.commit_running_ || self.input_ended_)
    return ERR_MEM;  // lwIP retains the pbuf until a later callback.
  self.packet_ = packet;
  return ERR_OK;
}

void ProofUploadServer::failed(void* arg, err_t) {
  auto& self = *static_cast<ProofUploadServer*>(arg);
  self.pcb_ = nullptr;  // lwIP destroyed it; poll disposes application state.
}

err_t ProofUploadServer::sent(void* arg, tcp_pcb*, std::uint16_t bytes) {
  auto& self = *static_cast<ProofUploadServer*>(arg);
  if (self.response_written_ && bytes) self.response_acked_ = true;
  return ERR_OK;
}

void ProofUploadServer::refuse(char response) {
  if (receiver_ && !commit_running_) receiver_->abort();
  commit_pending_ = false;
  queue_response(response);
}

void ProofUploadServer::queue_response(char response) {
  response_ = response;
  response_at_ = last_poll_at_;
}

void ProofUploadServer::poll(bool physically_armed, std::uint32_t now) {
  if (!occupied_) return;
  last_poll_at_ = now;
  if (!pcb_) { clear(); return; }
  if (!accepted_at_) accepted_at_ = now ? now : 1;
  if (!response_ && !commit_running_ &&
      static_cast<std::uint32_t>(now - accepted_at_) >= 5000)
    refuse('T');

  if (packet_ && !response_ && !commit_running_) {
    std::uint8_t bytes[256];
    while (offset_ < packet_->tot_len) {
      const std::size_t remaining = packet_->tot_len - offset_;
      const auto count = static_cast<std::uint16_t>(remaining < sizeof(bytes)
                                                       ? remaining : sizeof(bytes));
      const auto copied = pbuf_copy_partial(packet_, bytes, count, offset_);
      if (copied != count) { refuse('I'); break; }
      offset_ += copied;
      tcp_recved(pcb_, copied);
      const auto status = receiver_->feed(bytes, copied, physically_armed);
      if (status == UploadStatus::denied) { refuse('D'); break; }
      if (status == UploadStatus::invalid) { refuse('I'); break; }
      if (status == UploadStatus::ready && offset_ < packet_->tot_len) {
        refuse('I'); break;
      }
    }
    if (offset_ == packet_->tot_len || response_) discard_packet();
  }
  if (input_ended_ && !packet_ && !response_ && !commit_running_) {
    if (receiver_->status() == UploadStatus::ready) commit_pending_ = true;
    else refuse('I');
  }

  if (!response_) return;
  if (static_cast<std::uint32_t>(now - response_at_) >= 2000) {
    clear();
    return;
  }
  if (!response_written_) {
    if (!tcp_sndbuf(pcb_)) return;
    if (tcp_write(pcb_, &response_, 1, TCP_WRITE_FLAG_COPY) != ERR_OK) return;
    response_written_ = true;
    tcp_output(pcb_);
  }
  if (!response_acked_) return;
  tcp_pcb* old = pcb_;
  detach(old);
  const err_t result = tcp_close(old);
  if (result == ERR_OK) {
    pcb_ = nullptr;
    clear();
  } else if (result == ERR_MEM) {
    tcp_arg(old, this);
    tcp_recv(old, receive);
    tcp_err(old, failed);
    tcp_sent(old, sent);
  } else {
    clear();
  }
}

std::uint32_t ProofUploadServer::take_commit() {
  if (!commit_pending_ || !pcb_) return 0;
  commit_pending_ = false;
  commit_running_ = true;
  return owner_;
}

void ProofUploadServer::cancel_commit(std::uint32_t owner, char response) {
  if (!commit_running_ || owner != owner_) return;
  stage_.abort(owner);
  commit_running_ = false;
  queue_response(response);
}

void ProofUploadServer::finish_commit(std::uint32_t owner, SwitchReport report) {
  if (!commit_running_ || owner != owner_) return;
  commit_running_ = false;
  if (report.status == SwitchStatus::complete) queue_response('K');
  else if (report.status == SwitchStatus::timeout) queue_response('T');
  else if (report.status == SwitchStatus::denied) queue_response('D');
  else queue_response('F');
}

}  // namespace firmingo_managed
