# Arduino-Pico DHCP provenance

`upstream/dhcpserver.c` and `.h` are unmodified files from Arduino-Pico **6.0.0**,
`libraries/WiFi/src/dhcpserver/`, originally MicroPython code by Damien P. George.
Both retain the complete MIT license. `SHA256SUMS` identifies these exact inputs.

Source archive: https://github.com/earlephilhower/arduino-pico/releases/download/6.0.0/rp2040-6.0.0.zip

Archive SHA-256:
`d2163a321748be872a29cb99ead739840990d3ddf0935de756eb52f9fc2b5d46`.

The modified implementation is `src/ports/arduino_pico/local_dhcp.c` and `.h`.
`local-only.patch` records its complete diff from these upstream files. Its public
symbols are prefixed `firmingo_dhcp_` to avoid replacing the core's DHCP server.
The qualification sketch explicitly instantiates this server; no Boards Manager
files are patched.

Changes and reasons:

- Whitelist reply options; never include router (3), DNS (6), classless routes
  (121/249), or copied client/vendor options. This is a local endpoint.
- Validate cookie, request header and bounded TLVs, accept PAD and reordered
  options, require END, reject truncated/duplicate critical options. The original
  fixed-order/unbounded search could misinterpret input.
- Eight fixed leases: reserve offers, support retries, SELECTING, INIT-REBOOT
  with a known lease, renew/rebind via `ciaddr`, expiry, release and decline.
  Unknown INIT-REBOOT/renew bindings are silent; clients must restart discovery.
- Use 64-bit monotonic time, so long idle periods and the 32-bit millisecond
  boundary cannot make stale leases appear active.
- Match bounded client identifiers when present and echo them in replies;
  otherwise use Ethernet MAC. Keep failed allocation/send from committing a lease.
- Return allocation/bind failures and free each pbuf. Process under the port's
  lwIP lock; static RX/TX scratch buffers avoid large IRQ-stack allocations.

This remains a limited local-link server: Ethernet/IPv4, one /24, eight addresses
`.16–.23`, 548-byte maximum DHCP payload, client IDs up to 32 bytes, no relays or
option overload, no persistence and no multi-device addressing. Initial replies
broadcast; renewal/INFORM replies use `ciaddr`. Leases last 600 s, offers 30 s,
declined addresses are quarantined 60 s. It does not perform conflict probes;
the private dedicated-link pool must not contain other static hosts.

Native tests compile this exact C implementation against small fake lwIP/clock
adapters. They establish parser and reply behavior, not real-stack compatibility.
A separate fixed-seed mutation job runs 20,000 cases under ASan/UBSan; it is not
coverage-guided fuzzing. Hardware lease/routing and iOS qualification remain
separate requirements.

Protocol references: https://www.rfc-editor.org/rfc/rfc2131 and
https://www.rfc-editor.org/rfc/rfc2132 (plus client-ID echo from RFC 6842).
