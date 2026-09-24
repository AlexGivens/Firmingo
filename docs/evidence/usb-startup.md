# Controlled USB startup evidence — 2026-09-16

Target: user-selected Arduino Nano RP2040 Connect; revision and cable/hub details
unrecorded. Host: MacBookAir10,1, macOS 27.0 (26A428). Arduino-Pico 6.0.0, CLI1.5.1,
Pico SDK USB, 125 MHz, IPv4-only, no filesystem, default UF2 upload method.
Device USB serial and diagnostic ID: `a1b2c3d4e5f60718` (current firmware format).
UF2 SHA-256: `de41aa1af5f794d1c7b22e18be4223312ddfb1c4aea1dcf583d348855740c778`;
UF2 size: **268,288 bytes**.

## What changed

[Opt-in pinned patch](../../patches/arduino-pico-6.0.0-usb-startup/README.md):
main prepares USB synchronization before setup without starting the controller.
NCM begin registers its descriptor without the upstream disconnect/connect pair.
The qualification sketch builds DHCP/NCM/listeners while detached, then calls
USB.begin once with final NCM-only descriptors. CDC is disabled; the 250 ms sketch
delay and CDC logging were removed. Startup failures stay detached with a fault
blink. Source/HTTP and the bounded echo backend retain their prior behavior.

`/diagnostics` reports stable RP2040 ID, board/firmware/profile and startup times.
The smoke harness can now require this exact ID before opening stream sockets.
This page is a qualification interface, not the draft public protocol.

The build validates clean installed 6.0.0 files, copies only patched files to an
isolated data/build overlay, links dependencies, applies a no-fuzz patch and checks
resulting hashes. Installed sources and original baseline sketch remain unchanged.
Patch provenance/license and per-file pins are recorded with the patch.

## Commands actually run

```sh
.venv/bin/python tools/dev.py test --sanitize
.venv/bin/python -m pytest tests/host -q
ARDUINO_CONFIG_FILE="$PWD/arduino-cli.local.yaml" ARDUINO_CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' .venv/bin/python tools/dev.py build --board nano_rp2040_connect --firmware local-only
.venv/bin/python tools/dev.py flash --board nano_rp2040_connect --mount /Volumes/RPI-RP2
.venv/bin/python -m pytest tests/hardware -v -s --board nano_rp2040_connect --address 192.168.77.1 --allow-legacy-no-identity --junitxml=test-results/usb-startup-smoke.xml
.venv/bin/python tools/dev.py smoke --board nano_rp2040_connect --address 192.168.77.1 --device-id a1b2c3d4e5f60718
```

Initial upload used one intentional CDC1200-baud request on the previous running
qualification image at `/dev/cu.usbmodem101`, waited for ROM volume, then used the
explicit, hash-verified flash command. No reset/reflash was done after upload.
**The new NCM-only image has no CDC software upload-reset path.** Future uploads
require the previously exercised Nano REC/GND ROM recovery procedure.

Results:

- Five native suites pass with ASan/UBSan: 28 Unity cases (8 stream, 4 TCP echo,
  4 deferred startup, 2 default startup, 10 DHCP). Startup tests compile the actual
  patched function bodies/core USB block and sketch setup against fake effects.
  They cover idempotent prepare/start, service-before-attach ordering, each startup
  failure staying detached, late NCM registration rejection, and default behavior.
  This is not a USB packet trace; full firmware builds check headers/translation units.
- Final host harness suite: 39 passed. New tests validate overlay copy isolation,
  dirty-core/patch/target rejection and bounded identity responses/mismatched IDs.
  Loopback tests require socket permission in the sandbox.
- Build: 116,588 bytes program, 76,808 bytes static RAM; within board budgets.
  Existing upstream overloaded-virtual WiFiClient warnings remain. UF2 268,288 B.
- Firmware main disassembly calls USB.prepare before setup. SDK precompiled
  libpico symbol inspection found no USBClass/SerialUSB references requiring a
  changed-class ABI. Symbol presence of SerialUSB methods does not mean CDC was
  started; actual descriptors and diagnostics were checked separately.

## Hardware observation, including initial failure

The first post-upload observation found **no en5 interface**, and the first
`curl --max-time 5 http://192.168.77.1/diagnostics` timed out. USB initially showed a
Nano device without bound interfaces. Later inspection showed configuration1,
NCM control interface0/class2/subclass13 and data interface1/class10, bound to
AppleUSBNCMControl/Data and en5. No CDC serial interface was present.

**The initial readiness failure is retained.** Host binding subsequently appeared
without an assistant reset/reflash, but the actual authorization/configuration
wait duration and cause are unknown. No permission-prompt observation was recorded.
Do not infer firmware timing, authorization success rate or cold-start latency
from that delayed success. This was an upload reset, not a physical cold connection.

After binding:

```json
{"firmware":"ncm-startup-v1","board":"nano_rp2040_connect","device_id":"a1b2c3d4e5f60718","usb_profile":"ncm-only","usb_initialized_at_setup":false,"setup_ms":1,"dhcp_ready_ms":2,"ncm_ready_ms":2,"services_ready_ms":3,"attach_requested_ms":3}
```

These are board-relative readiness/attach-request timestamps, not host lease time.
The reported ID was compared with the actual USB serial before the first stream
run; the first run used the legacy harness flag. After harness improvement, the
second run verified identity itself before stream traffic.

- First smoke: **3 passed in 7.17 s**, including exact 65,536-byte echo,
  4,096-byte reconnect, and concurrent source/slow reader.
- Identity-verified smoke: **3 passed in 7.21 s**. Exact source 5,440 B in
  0.963913 s; echo 65,536 B in 3.014205 s (21,742 B/s); reconnect4,096 B in
  0.356970 s. Concurrent echo65,536 B in 2.818825 s (23,249 B/s), source5,440 B
  in 0.992397 s.
- Independent concurrency check: echo65,536 B in 2.785903 s and source27,200 B
  in 4.998681 s; fresh curl IPv4 and IPv6 HTTPS returned200 during both transfers.
- DHCP assigned192.168.77.16/24, ACK omitted router/DNS, board route stayed en5,
  Internet default route stayed Wi-Fi en0 via192.168.4.1. No host network settings
  were changed.

[Flashed build and dependency hashes](usb-startup-flashed-build.json),
[post-flash diagnostics/routes/DHCP](usb-startup-postflash.json),
[concurrent traffic and USB interface summary](usb-startup-coexistence.json).
The first attempt to save concurrency results failed decoding a binary MAC byte
in ioreg as UTF-8, after traffic completed. Recording was corrected to replace
invalid text bytes, then repeated successfully; this was a recording-tool failure.
Local logs: build/usb-startup-build.log, build/usb-startup-tests.log,
build/usb-identity-tests.log, build/usb-startup-smoke.log,
build/usb-startup-identity-smoke.log, test-results/usb-startup-smoke.xml.
The final build after host-tool changes produced the identical UF2 hash shown
above; another flash was unnecessary. The saved flashed-build report preserves
the metadata at upload time; current build output contains the final tool hashes.

## Not completed

Subsequent [physical macOS reconnections](macos-cold.md): **3 functional passes / 3
attempts toward 20 cycles**; no new prompt reported. Cycle 1 latency was unmeasured;
cycle 2 host-observed USB-to-address was 2.864 s and USB-to-identity 2.882 s.
Cycle 3 observed intervals were 2.926 s and 2.956 s respectively.
iPhone/iPad cold-connection count remains **0/20**. No claim of
qualified automatic no-RESET startup, stable first authorization, locked/denied
attachment or cellular coexistence. No USB analyzer trace was captured. Memory
stability/soak and sustained TCP zero-window handling remain unmeasured; HTTP and
source still need bounded partial-write handling. Firmware control/UART/update
capabilities remain unimplemented.

Next: collect the remaining physical macOS cycles with measured readiness timing
and available iPhone/iPad authorization/coexistence evidence. The first post-upload
timeout above remains the historical result for that attempt.
