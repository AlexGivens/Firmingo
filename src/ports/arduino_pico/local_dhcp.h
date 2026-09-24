/*
    This file is part of the MicroPython project, http://micropython.org/

    The MIT License (MIT)

    Copyright (c) 2018-2019 Damien P. George

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
*/

#ifndef FIRMINGO_LOCAL_DHCP_H
#define FIRMINGO_LOCAL_DHCP_H
#include <stdint.h>
#include <lwip/ip_addr.h>
#include <lwip/netif.h>
#ifdef __cplusplus
extern "C" {
#endif
#define FIRMINGO_DHCP_BASE_IP 16
#define FIRMINGO_DHCP_MAX_IP 8
#define FIRMINGO_DHCP_CLIENT_ID_MAX 32

typedef struct {
    uint8_t key[FIRMINGO_DHCP_CLIENT_ID_MAX];
    uint8_t key_len, has_id, state; // 0 free, 1 offered, 2 bound, 3 declined
    uint64_t since_ms;
    uint32_t duration_ms;
} firmingo_dhcp_lease_t;
typedef struct {
    ip_addr_t ip, nm;
    firmingo_dhcp_lease_t lease[FIRMINGO_DHCP_MAX_IP];
    struct udp_pcb *udp;
    uint32_t received, replies, ignored, send_errors;
} firmingo_dhcp_server_t;

// Application-context calls must hold the port's lwIP lock. Supports /24 only.
int firmingo_dhcp_init(firmingo_dhcp_server_t *, const ip_addr_t *, const ip_addr_t *, struct netif *);
void firmingo_dhcp_deinit(firmingo_dhcp_server_t *);
#ifdef __cplusplus
}
#endif
#endif
