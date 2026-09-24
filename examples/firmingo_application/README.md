# RP2040 application-stream reference

Experimental FMGO v1 TCP endpoint on `192.168.77.1:7420`. Firmingo **0.1.0 beta**
provides a bounded application byte-stream endpoint. The exact 0.1.0 image is
compile-tested until fresh hardware checks are recorded; the historical V7
results below belong to an earlier UF2. The
reference `loop()` explicitly reads host bytes and writes them back; the backend
itself no longer supplies echo behavior. It replaces neither a UART nor a runtime
console.
HTTP/source/legacy raw echo belong to the separate qualification sketch.

```sh
.venv/bin/python tools/dev.py build --board nano_rp2040_connect --firmware application
.venv/bin/python tools/dev.py flash --board nano_rp2040_connect --firmware application --mount /Volumes/RPI-RP2
.venv/bin/python tools/dev.py smoke --board nano_rp2040_connect --firmware application --address 192.168.77.1 --device-id a1b2c3d4e5f60718
```

The same source is hardware-tested on `raspberry_pi_pico`: full FMGO smoke,
622,592 exact bytes with required diagnostics across 10 sessions, and a seeded
65,536-byte exact transfer containing fresh IPv4/IPv6 Internet requests pass.
Select your own board's verified ID. Flash requires manually entering the ROM
bootloader; see [recovery](../../docs/usb-startup.md). Smoke does not flash/reset or
change host networking. Build uses pinned Arduino-Pico 6.0.0, CLI 1.5.1, Pico SDK
USB stack, IPv4-only, 125 MHz, Small optimization, exceptions/RTTI disabled and
16 MiB no-filesystem layout. Select `--subnet-octet` at build time to avoid
host LAN/VPN overlap. DHCP supplies no router/DNS; NINA remains unused.

[Wire contract](../../docs/protocol.md): hello before channel 1 acquisition,
separate JSON control and opaque binary data frames, one owner, two bounded TCP
connections. Excess sockets receive RST; the second session can receive BUSY.
Disconnect discards queued bytes without replay. Fatal JSON replies get bounded
best-effort flushing/acknowledgment before socket closure. Boot ID is a random
64-bit nonce per boot, not a secret/authenticator or guaranteed unique counter.

Application code uses `ApplicationEndpoint::active()`, `available()`,
`read_from_host()`, `available_for_write()` and `write_to_host()`. Reads/writes
are nonblocking and may be partial. Read no more input than the outbound queue can
accept when implementing an echo or request processor. Calls run serially in the
Arduino application loop; they are not ISR-safe. No TCP, FMGO, USB or lwIP headers
are needed in the application logic.

Each direction has a 256-byte queue. A full queue applies backpressure and drops
nothing. Open clears old queue contents, marks the endpoint active and increments
a saturating connection generation. Close, timeout, I/O failure and disconnect
mark it inactive and discard both queues. Data cannot be queued while inactive,
so a later owner receives no replay. `discarded_from_host()` and
`discarded_to_host()` count bytes intentionally disposed during lifecycle cleanup.
Application queue pending/peak/discard counters are also reported by
`device.diagnostics`.

Do not inject diagnostics into application bytes. This open development build
supports no reset, UART controls, upload, runtime console, mDNS or updater.

Compile/native evidence is separate from hardware qualification. The previous
`application-dev-v1` Loopback image passed macOS smoke, a measured soak and a
64 KiB exact transfer with concurrent Wi-Fi Internet access. V2 passes smoke,
an exact 1.25 MiB measured soak, isolated 64 KiB and a two-second reader stall.
Four unassisted 64 KiB transfers overlapping a new Internet request stalled. The
fourth was packet-captured and implicated the pinned NCM receive worker after its
ten-packet budget. V3 directly marked the worker pending, but its first isolated
64 KiB hardware replay failed with the same 6,656-byte return. V4 calls the SDK
async-context wake API after releasing the USB mutex and exposes persistent NCM/
raw-TCP counters. It passes native/host/build checks, full hardware smoke, one
isolated 64 KiB replay, one Internet-overlap replay and a 1.25 MiB two-minute soak.
The hardware budget/wake counters remained zero even during a refined concurrent
two-connection burst, so the continuation branch is native-tested but not directly
demonstrated on the Nano. V5's persistent maximum-frames-per-worker-run measurement
reached only 2 during full smoke, the refined burst and an Internet-overlap replay;
all byte/control checks passed.
V6 also counts NCM worker attempts that fail to acquire the USB mutex. It passes
full hardware smoke and one unretried 64 KiB Internet-overlap replay. The contention
counter remained zero and the receive batch peak remained 2, so that replay did
not exercise either suspected NCM worker path.
V7 retains the same application capability and incorporates the generic backend
configuration boundary used by the separate UART profile. It now passes full
hardware smoke and a bounded diagnostics run with concurrent Internet access.
See [hardware evidence](../../docs/evidence/application-hardware.md); FMGO on iOS
remains untested. The raw port explicitly dispatches lwIP timers in each poll, since
frequent lock releases can postpone the SDK background timeout worker.
