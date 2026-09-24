#pragma once
#include "ports/arduino_pico/local_dhcp.h"
// Configurable /24, device .1, leases .16 through .23. Choose a subnet that
// does not overlap the host's LAN/VPN routes. Multiple boards are not supported.
#ifndef FIRMINGO_NET_A
#define FIRMINGO_NET_A 192
#endif
#ifndef FIRMINGO_NET_B
#define FIRMINGO_NET_B 168
#endif
#ifndef FIRMINGO_NET_C
#define FIRMINGO_NET_C 77
#endif
#if FIRMINGO_NET_A != 192 || FIRMINGO_NET_B != 168 || FIRMINGO_NET_C < 0 || FIRMINGO_NET_C > 255
#error "Qualification configuration must be a 192.168.x.0/24 subnet"
#endif
