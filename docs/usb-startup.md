# Controlled initial USB attachment

The local-only qualification build now uses **NCM-only USB**. It registers the
NCM descriptor while the controller is off, starts DHCP/netif/listeners, then
initializes USB once. It does not open CDC, wait for a terminal, add a startup
delay or explicitly reconnect. This targets the automatic attachment problem;
iOS authorization/startup still require real-device evidence.

The pinned Arduino-Pico 6.0.0 main previously initialized USB before `setup()`.
Its automatic CDC begin and NCM begin then each disconnected/reconnected to add
interfaces. Merely removing the latter would leave the earlier attachment. The
[reviewable core patch](../patches/arduino-pico-6.0.0-usb-startup/README.md) addresses
both paths. The default baseline build uses the clean core and stays preserved.

Build using the usual explicit command:

```sh
.venv/bin/python tools/dev.py build --board nano_rp2040_connect --firmware local-only
```

Choose the pinned CLI/core through the environment variables documented in
[network setup](local-only-network.md). The tool validates upstream file hashes,
creates a separate patched Arduino data overlay under build output, and records
patch/version/hash information. `patch` and Python are required; doctor checks
tools without installing them. Other USB stacks, FreeRTOS, CDC and Picotool USB
are unsupported for this qualification profile. Use tools/dev.py for this patched
image; stock IDE build settings cannot reproduce it without this overlay.

Flash only an explicitly selected ROM bootloader volume. **There is no CDC serial
port or 1200-baud software-reset path after this image boots.** Recovery remains:
unplug; connect REC to GND (not RESET); plug into the Mac; remove the jumper after
RPI-RP2 appears, leave USB connected, then run:

```sh
.venv/bin/python tools/dev.py flash --board nano_rp2040_connect --mount /Volumes/RPI-RP2
```

This is the Nano's previously exercised ROM recovery, independent of Firmingo.
A startup fault stays detached and toggles D13/GPIO6 LED every 100 ms; use REC/GND
recovery. During normal operation LED remains a USB-mounted hint, not proof of
NCM activation, DHCP or TCP connectivity. The NINA RGB LED is not used.

## Diagnostics and qualification

`http://192.168.77.1/diagnostics` reports a stable RP2040 board ID, firmware marker,
NCM-only profile, whether TinyUSB was initialized when setup began (must be false),
and monotonic setup/DHCP-ready/NCM-ready/listener-ready/attach-request timestamps.
Attach request is recorded immediately before USB.begin; it is **not** a measured
host authorization/configuration or lease time. Source/HTTP remain prototype
services; this diagnostic endpoint is not the future public control contract.

Use the selected diagnostic/USB ID for nondestructive smoke; on this board:

```sh
.venv/bin/python tools/dev.py smoke --board nano_rp2040_connect --address 192.168.77.1 --device-id a1b2c3d4e5f60718
```

The harness checks the reported ID before stream traffic; another board needs its
own expected ID. Desktop validation checks actual host interfaces, reported timestamps and identity,
exact byte smoke, DHCP router/DNS omission, and fresh Internet requests. USB source
inspection/native fake effects establish ordering; they cannot prove host-visible
packet timing. Inspect actual host USB descriptors separately.

For each available macOS/iPhone/iPad target, aim for **20 real cold connections**:

| Action | Expected result | Observed |
| --- | --- | --- |
| Unplug board; confirm USB interface gone; reconnect | Only final NCM interface appears; authorization prompt stable when applicable | Record cycle/host/OS |
| Wait for automatic DHCP without pressing RESET | Correct non-overlapping local address, no router/DNS; record time from physical insertion | Record lease time and pass/fail |
| Open diagnostics and stream | Correct stable ID/profile; all startup-ready timestamps precede attach request | Record diagnostics |
| Stream while opening fresh HTTPS via existing Wi-Fi/cellular | Exact bytes; Internet still accessible | Record outcome |

A connection requiring RESET or manual IP edits is a failure. Do not reset privacy
settings to manufacture first-authorization evidence. Keep first prompt, denial,
locked/unlock, previously authorized reconnection and board-reset checks separate
in [testing.md](testing.md). A software upload reset is not a cold physical plug.
Results: [USB startup evidence](evidence/usb-startup.md).

For host-observed readiness timing and repeated physical cycles, use the
[macOS attachment command](cold-attachment.md). It preserves each failure and
does not claim exact physical-insertion or packet-level lease latency.
