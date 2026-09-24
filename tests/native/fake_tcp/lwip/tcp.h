#pragma once
#include <cstddef>
#include <cstdint>
using err_t = int;
constexpr err_t ERR_OK=0, ERR_MEM=-1, ERR_ABRT=-2, ERR_CONN=-3;
constexpr int IPADDR_TYPE_V4=0, TCP_WRITE_FLAG_COPY=1;
struct ip_addr_t {};
struct pbuf { uint16_t tot_len; uint8_t* payload; pbuf* next = nullptr; uint16_t len = 0; };
struct tcp_pcb {
  void* arg = nullptr;
  err_t (*accept)(void*, tcp_pcb*, err_t) = nullptr;
  err_t (*recv)(void*, tcp_pcb*, pbuf*, err_t) = nullptr;
  void (*error)(void*, err_t) = nullptr;
  err_t (*sent)(void*, tcp_pcb*, uint16_t) = nullptr;
  uint16_t room = 256;
  bool aborted = false;
};
tcp_pcb* tcp_new_ip_type(int);
err_t tcp_bind(tcp_pcb*, const ip_addr_t*, uint16_t);
tcp_pcb* tcp_listen_with_backlog(tcp_pcb*, int);
inline void tcp_arg(tcp_pcb* p, void* a) { p->arg=a; }
inline void tcp_accept(tcp_pcb* p, decltype(tcp_pcb::accept) f) { p->accept=f; }
inline void tcp_recv(tcp_pcb* p, decltype(tcp_pcb::recv) f) { p->recv=f; }
inline void tcp_err(tcp_pcb* p, decltype(tcp_pcb::error) f) { p->error=f; }
inline void tcp_nagle_disable(tcp_pcb*) {}
inline uint16_t tcp_sndbuf(tcp_pcb* p) { return p->room; }
inline void tcp_sent(tcp_pcb* p, decltype(tcp_pcb::sent) f) { p->sent=f; }
err_t tcp_close(tcp_pcb*);
void tcp_abort(tcp_pcb*);
void tcp_recved(tcp_pcb*, uint16_t);
err_t tcp_write(tcp_pcb*, const void*, uint16_t, int);
err_t tcp_output(tcp_pcb*);
void pbuf_free(pbuf*);
uint16_t pbuf_copy_partial(pbuf*, void*, uint16_t, uint16_t);
