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

// Firmingo modifications: local-only options, bounded parser and lease handling.
#include "local_dhcp.h"
#include <lwip/udp.h>
#include <pico/time.h>
#include <string.h>
#include <errno.h>

#define OFFER 2
#define ACK 5
#define NAK 6
#define LEASE_MS 600000u
#define OFFER_MS 30000u
#define DECLINE_MS 60000u

// Wire offsets, not a packed/native-endian struct. Fixed bound includes options.
#define OPTIONS 240
#define MAX_PACKET 548
static const uint8_t cookie[4] = {99, 130, 83, 99};

typedef struct {
    uint8_t type;
    const uint8_t *requested, *server, *client;
    uint8_t client_len;
} options_t;

// Validate the entire option stream before interpreting it or touching leases.
// PAD and END are one byte. Overload is deliberately unsupported, not misparsed.
static int parse_options(const uint8_t *p, size_t len, options_t *o) {
    memset(o, 0, sizeof(*o));
    for (size_t i = OPTIONS; i < len;) {
        uint8_t code = p[i++];
        if (code == 0) continue;
        if (code == 255) return o->type != 0;
        if (i == len) return 0;
        uint8_t n = p[i++];
        if (n > len - i) return 0;
        switch (code) {
        case 53:
            if (n != 1 || o->type != 0 || p[i] == 0) return 0;
            o->type = p[i];
            break;
        case 50:
            if (n != 4 || o->requested) return 0;
            o->requested = p + i;
            break;
        case 54:
            if (n != 4 || o->server) return 0;
            o->server = p + i;
            break;
        case 61:
            if (n < 2 || n > FIRMINGO_DHCP_CLIENT_ID_MAX || o->client) return 0;
            o->client = p + i;
            o->client_len = n;
            break;
        case 52: return 0;
        default: break;
        }
        i += n;
    }
    return 0; // END is required; never read stale bytes after this datagram.
}

static int zero(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; ++i) if (p[i]) return 0;
    return 1;
}
static int same_client(const firmingo_dhcp_lease_t *l, const uint8_t *key, uint8_t n, uint8_t has_id) {
    return l->state && l->state != 3 && l->key_len == n &&
           l->has_id == has_id && memcmp(l->key, key, n) == 0;
}
static int slot(const firmingo_dhcp_server_t *d, const uint8_t *ip) {
    if (memcmp(ip, ip_2_ip4(&d->ip), 3) != 0 || ip[3] < FIRMINGO_DHCP_BASE_IP ||
        ip[3] >= FIRMINGO_DHCP_BASE_IP + FIRMINGO_DHCP_MAX_IP) return -1;
    return ip[3] - FIRMINGO_DHCP_BASE_IP;
}
static void option(uint8_t **p, uint8_t code, const void *data, uint8_t n) {
    *(*p)++ = code; *(*p)++ = n;
    memcpy(*p, data, n); *p += n;
}
static void option32(uint8_t **p, uint8_t code, uint32_t value) {
    uint8_t data[4] = {(uint8_t)(value >> 24), (uint8_t)(value >> 16),
                       (uint8_t)(value >> 8), (uint8_t)value};
    option(p, code, data, 4);
}

// Called by lwIP while its context lock is held. Work and buffers are bounded.
static void receive(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                    const ip_addr_t *src, u16_t port) {
    firmingo_dhcp_server_t *d = arg;
    (void)pcb; (void)src;
    if (!p) return;
    ++d->received;
    // All invocations are serialized by the lwIP lock; no callback reentrancy.
    static uint8_t packet[MAX_PACKET];
    size_t len = p->tot_len;
    if (port != 68 || len < OPTIONS + 4 || len > sizeof(packet) ||
        pbuf_copy_partial(p, packet, (u16_t)len, 0) != len) goto ignored;
    if (packet[0] != 1 || packet[1] != 1 || packet[2] != 6 || packet[3] != 0 ||
        !zero(packet + 24, 4) || zero(packet + 28, 6) || (packet[28] & 1) ||
        memcmp(packet + 236, cookie, 4) != 0) goto ignored;
    options_t o;
    if (!parse_options(packet, len, &o)) goto ignored;
    if (o.server && memcmp(o.server, ip_2_ip4(&d->ip), 4)) goto ignored;
    const uint8_t *key = o.client ? o.client : packet + 28;
    uint8_t key_len = o.client ? o.client_len : 6;
    uint8_t has_id = o.client != NULL;
    uint64_t now = to_us_since_boot(get_absolute_time()) / 1000;
    int owned = -1, free_slot = -1;
    for (int i = 0; i < FIRMINGO_DHCP_MAX_IP; ++i) {
        firmingo_dhcp_lease_t *l = &d->lease[i];
        if (l->state && (now - l->since_ms) >= l->duration_ms) memset(l, 0, sizeof(*l));
        if (same_client(l, key, key_len, has_id)) owned = i;
        if (!l->state && free_slot < 0) free_slot = i;
    }
    int chosen = -1;
    uint8_t response = 0;
    if (o.type == 1) { // DISCOVER: reserve an offer so simultaneous clients differ.
        if (!zero(packet + 12, 4) || o.server) goto ignored;
        chosen = owned >= 0 ? owned : free_slot;
        if (chosen < 0) goto ignored;
        response = OFFER;
    } else if (o.type == 3) { // SELECTING/INIT-REBOOT or RENEWING/REBINDING.
        if (o.requested && !zero(packet + 12, 4)) goto ignored;
        if (o.server && !o.requested) goto ignored;
        const uint8_t *wanted = o.requested ? o.requested : packet + 12;
        if (zero(wanted, 4)) goto ignored;
        chosen = slot(d, wanted);
        if (chosen < 0 || (owned >= 0 && chosen != owned)) response = NAK;
        else if (chosen != owned) {
            // No binding record: stay silent on INIT-REBOOT/renew after restart.
            if (!o.server) goto ignored;
            response = NAK;
        } else response = ACK;
    } else if (o.type == 7) { // RELEASE, only for this client's bound address.
        if (!o.server || o.requested || owned < 0 || slot(d, packet + 12) != owned) goto ignored;
        memset(&d->lease[owned], 0, sizeof(d->lease[owned]));
        goto done;
    } else if (o.type == 4) { // DECLINE: quarantine a matching offer/binding.
        if (!o.server || !o.requested || owned < 0 || slot(d, o.requested) != owned) goto ignored;
        d->lease[owned].state = 3;
        d->lease[owned].since_ms = now;
        d->lease[owned].duration_ms = DECLINE_MS;
        goto done;
    } else if (o.type == 8) { // INFORM: settings only, no lease allocation.
        if (o.requested || zero(packet + 12, 4) || memcmp(packet + 12, ip_2_ip4(&d->ip), 3)) goto ignored;
        response = ACK;
    } else goto ignored;

    // Build from scratch. No incoming/vendor options, routes or DNS are copied.
    static uint8_t reply[MAX_PACKET];
    memset(reply, 0, sizeof(reply));
    reply[0] = 2; reply[1] = 1; reply[2] = 6;
    memcpy(reply + 4, packet + 4, 4); // xid opaque, unchanged
    memcpy(reply + 10, packet + 10, 2); // flags
    memcpy(reply + 28, packet + 28, 16); // hardware address
    memcpy(reply + 236, cookie, 4);
    if (response != NAK) {
        memcpy(reply + 12, packet + 12, 4);
        if (chosen >= 0) {
            memcpy(reply + 16, ip_2_ip4(&d->ip), 3);
            reply[19] = FIRMINGO_DHCP_BASE_IP + chosen;
        }
    }
    uint8_t *out = reply + OPTIONS;
    option(&out, 53, &response, 1);
    option(&out, 54, ip_2_ip4(&d->ip), 4);
    if (response != NAK) {
        option(&out, 1, ip_2_ip4(&d->nm), 4);
        if (chosen >= 0) {
            option32(&out, 51, LEASE_MS / 1000);
            option32(&out, 58, LEASE_MS / 2000);
            option32(&out, 59, LEASE_MS / 1000 * 7 / 8);
        }
    }
    if (o.client) option(&out, 61, o.client, o.client_len);
    *out++ = 255;
    // RFC BOOTP minimum message size, padding zeroed above.
    u16_t size = (u16_t)(out - reply);
    if (size < 300) size = 300;
    ip_addr_t destination;
    // Initial replies broadcast (no L2 unicast support); renew/INFORM unicast.
    if (response != NAK && !zero(packet + 12, 4))
        IP_ADDR4(&destination, packet[12], packet[13], packet[14], packet[15]);
    else IP_ADDR4(&destination, 255, 255, 255, 255);
    struct pbuf *tx = pbuf_alloc(PBUF_TRANSPORT, size, PBUF_RAM);
    if (!tx) { ++d->send_errors; goto done; }
    memcpy(tx->payload, reply, size);
    err_t result = udp_sendto(d->udp, tx, &destination, 68);
    pbuf_free(tx);
    if (result != ERR_OK) { ++d->send_errors; goto done; }
    ++d->replies;
    if (chosen >= 0 && response != NAK) {
        firmingo_dhcp_lease_t *l = &d->lease[chosen];
        // Discover retries must not shorten an already committed lease.
        if (!(response == OFFER && l->state == 2)) {
            memset(l, 0, sizeof(*l));
            memcpy(l->key, key, key_len);
            l->key_len = key_len; l->has_id = has_id;
            l->state = response == OFFER ? 1 : 2;
            l->since_ms = now;
            l->duration_ms = response == OFFER ? OFFER_MS : LEASE_MS;
        }
    }
    goto done;
ignored:
    ++d->ignored;
done:
    pbuf_free(p);
}

int firmingo_dhcp_init(firmingo_dhcp_server_t *d, const ip_addr_t *ip,
                       const ip_addr_t *mask, struct netif *netif) {
    // Caller supplies a zero-initialized or deinitialized object, under lwIP lock.
    if (d->udp) return -EALREADY;
    const uint8_t *bytes = (const uint8_t *)ip_2_ip4(ip);
    static const uint8_t supported_mask[4] = {255, 255, 255, 0};
    if (memcmp(ip_2_ip4(mask), supported_mask, 4) || bytes[3] == 0 || bytes[3] == 255 ||
        (bytes[3] >= FIRMINGO_DHCP_BASE_IP && bytes[3] < FIRMINGO_DHCP_BASE_IP + FIRMINGO_DHCP_MAX_IP)) return -EINVAL;
    memset(d, 0, sizeof(*d));
    ip_addr_copy(d->ip, *ip); ip_addr_copy(d->nm, *mask);
    d->udp = udp_new();
    if (!d->udp) return -ENOMEM;
    ip_addr_t any; IP_ADDR4(&any, 0, 0, 0, 0);
    err_t result = udp_bind(d->udp, &any, 67);
    if (result != ERR_OK) { udp_remove(d->udp); d->udp = NULL; return result; }
    if (netif) udp_bind_netif(d->udp, netif);
    udp_recv(d->udp, receive, d);
    return 0;
}
void firmingo_dhcp_deinit(firmingo_dhcp_server_t *d) {
    if (d->udp) udp_remove(d->udp);
    memset(d, 0, sizeof(*d));
}
