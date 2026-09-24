#pragma once
#include "ip_addr.h"
#include "netif.h"
#include <stddef.h>
struct pbuf { struct pbuf *next; void *payload; u16_t len, tot_len; };
struct udp_pcb;
typedef void (*udp_recv_fn)(void *, struct udp_pcb *, struct pbuf *, const ip_addr_t *, u16_t);
struct udp_pcb { udp_recv_fn callback; void *arg; struct netif *netif; };
#define PBUF_TRANSPORT 0
#define PBUF_RAM 0
struct udp_pcb *udp_new(void);
void udp_remove(struct udp_pcb *);
void udp_recv(struct udp_pcb *, udp_recv_fn, void *);
err_t udp_bind(struct udp_pcb *, const ip_addr_t *, u16_t);
void udp_bind_netif(struct udp_pcb *, struct netif *);
err_t udp_sendto(struct udp_pcb *, struct pbuf *, const ip_addr_t *, u16_t);
struct pbuf *pbuf_alloc(int, u16_t, int);
u16_t pbuf_copy_partial(const struct pbuf *, void *, u16_t, u16_t);
uint8_t pbuf_free(struct pbuf *);
