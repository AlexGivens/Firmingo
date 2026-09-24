# Testing and hardware evidence

Run `.venv/bin/python tools/dev.py test --sanitize` for board-free checks.
No hardware is contacted by this command. Run `--help` on each subcommand for
arguments. Current results are in [the foundation report](evidence/foundation.md).

## Current local-only qualification

The original baseline interrupted Internet access during the first hardware run.
Use [bootloader-only replacement and coexistence checks](local-only-network.md)
before further stream stress. For the NCM-only image, see
[controlled startup/recovery](usb-startup.md) and [recorded results](evidence/usb-startup.md).
Current network results: [local-only evidence](evidence/local-only.md).
Prepared iPhone/iPad image and attempt ledger:
[iOS qualification evidence](evidence/ios-qualification.md).
The original-address command below is historical baseline tooling, not the next
command to run on the disconnected board.

## Timed desktop reconnections

[The macOS attach command](cold-attachment.md) observes the selected board's
USB disappearance/reappearance, checks local DHCP/routes/identity, then verifies
exact concurrent bytes and Internet access. It records host-observed readiness
intervals and saves stage/failure checkpoints. No reset/flash/host network changes
or retries rescue a failed cycle. User actions and authorization remain manual.
This does not certify iOS or measure exact physical insertion/DHCP exchange time.

## Qualification smoke with verified identity

For the qualification firmware profile, confirm diagnostics against the selected physical
board/USB serial, then use its exact stable ID:

```sh
.venv/bin/python tools/dev.py smoke --board nano_rp2040_connect --address 192.168.77.1 --device-id a1b2c3d4e5f60718
```

Select your own board ID on another Nano; do not copy this ID as a universal default.
The harness reads bounded `/diagnostics` data before stream traffic and fails on
ID/board mismatch. It never resets/flashes or changes host routes. No CDC port is
expected for NCM-only; inspect actual USB NCM interfaces instead. A successful
smoke after binding does not erase an earlier attachment timeout.

## Application protocol smoke

After explicitly flashing the [new application reference](../examples/firmingo_application/README.md),
use `--firmware application` to select FMGO on TCP 7420:

```sh
.venv/bin/python tools/dev.py smoke --board nano_rp2040_connect --firmware application --address 192.168.77.1 --device-id a1b2c3d4e5f60718
```

This command validates the current 0.1.0 beta image after it is explicitly
flashed. The prior `application-dev-v7` evidence does not qualify this new UF2.
It validates
hello identity/capabilities before stream commands and verifies exact fragmented
binary echo, a delayed reader, second-client BUSY, backend counters, unsupported
reset, ownership transfer and a fresh reconnect. It does not reset/flash or modify
host routes, and never retries a failed connection automatically. Check actual
routing, DHCP router/DNS omission and an uncached Internet request independently.
One full macOS smoke and a 64 KiB exact transfer with concurrent Internet access
pass after explicit timer servicing. See [hardware results](evidence/application-hardware.md)
and [the integration report](evidence/tcp-session.md).
Repeated physical cold tests remain paused at 3/3 by user request; iOS remains
untested for this application image.

The Raspberry Pi Pico now has separate local-only and application hardware
evidence on macOS. Local-only source/echo/reconnect and Internet coexistence pass;
application v7 passes FMGO identity/capabilities, ownership, exact bytes,
reconnect, a one-minute diagnostics soak, and a contained exact-transfer Internet
check. See [Pico evidence](evidence/raspberry-pi-pico.md). This does not add Pico
UART, repeated attachment, iOS, or long-duration qualification.

## UART loopback smoke

The 0.1.0 beta UART image must be explicitly flashed before this check.
With the board unpowered, remove external circuits and connect Nano D1/TX directly
to D0/RX. Both are 3.3 V signals. Enter ROM bootloader mode and flash the verified
UART image as described in the [UART reference](../examples/firmingo_uart/README.md),
then run:

```sh
.venv/bin/python tools/dev.py smoke --board nano_rp2040_connect --firmware uart --address 192.168.77.1 --device-id a1b2c3d4e5f60718
```

Use the stable ID reported by the selected physical Nano. The command validates
the `0.1.0` identity and UART capability, rejects a second owner, checks
16 KiB of exact loopback at 115200 8N1, acknowledges and verifies reconfiguration
to 57600 8N1, then checks 4 KiB with a delayed reader. It requires exactly 20,480
bytes in each direction and zero observed UART overrun events/minimum lost bytes.
It does not flash, reset, alter routes, or retry a failed connection. Verify the
host default route and a fresh Internet request separately. UART v1's first
bounded smoke/rerun/coexistence check passed, but its first measured soak later
failed with RX overrun. V2 passes the same unretried 60-second replay with 458,752
exact bytes, exercised throttling, zero overrun/loss, and Wi-Fi Internet
coexistence; see [UART evidence](evidence/uart.md).

For independent-direction testing, flash the test-only
[Pico peer fixture](../tests/hardware/pico_uart_peer/README.md), cross TX/RX and
share ground without connecting power rails, then select its USB CDC port with
`tools/uart_peer_smoke.py`. The harness validates deterministic Nano-to-Pico and
Pico-to-Nano bytes separately at 115200 and 57600. `tools/uart_overrun.py` is a
separate explicit command that sends an unsolicited bounded burst, requires
overrun/loss diagnostics, drains stale RX on acquisition, and verifies exact
recovery. It is not part of ordinary smoke because the boot-lifetime loss counters
remain nonzero until the Nano reboots. `tools/uart_format_matrix.py` uses the v2
peer fixture to check all 24 data/parity/stop-bit combinations, bounded baud
endpoints, atomic invalid-setting rejection, and exact traffic in both directions:

```sh
.venv/bin/python tools/uart_format_matrix.py \
  --address 192.168.77.1 --device-id a1b2c3d4e5f60718 \
  --board nano_rp2040_connect --peer-port /dev/cu.usbmodemXXXX \
  --output build/uart-format-matrix.json
```

The default 2 Mbaud case is deliberately shorter than the 256-byte Nano RX FIFO.
Use a separately named evidence file for larger no-CTS burst characterization;
such a burst can be expected to overrun and must never be reported as a matrix pass.

## NCM receive-budget diagnostic

This opt-in command opens exactly two FMGO connections, sends one 64 KiB exact
owner stream alongside 64 recoverable padded control requests, and compares the
boot-lifetime NCM budget/wake counters. It never flashes, resets, retries or changes
host networking, and refuses to overwrite an evidence file:

```sh
.venv/bin/python tools/ncm_burst.py \
  --address 192.168.77.1 \
  --device-id a1b2c3d4e5f60718 \
  --board nano_rp2040_connect \
  --firmware-sha256 add0366da36c237bb693615911cb0de9ff7d4c8e16908d4386795abba8f94d69 \
  --output build/ncm-burst.json
```

Exit 0 means exact traffic passed and at least one matched budget-exhaustion/wake
pair was observed. Exit 2 means traffic passed but the branch was not exercised;
that is recorded as `not_exercised`, not a hardware pass. Any byte, response,
identity, counter or connection failure exits 1 and remains a failure.

## One-board preserved-baseline smoke

Before opening test sockets, confirm the board is visible over USB and that the
selected address routes through its USB Ethernet interface. On macOS, inspect
`arduino-cli board list`, `ifconfig`, `networksetup -listallhardwareports`, and
`route -n get <board-address>`. Do not infer identity from a reachable IP alone.
The prototype's `192.168.7.0/24` overlaps this development Mac's Wi-Fi network
(`192.168.4.0/22`); on 2026-09-16, with no Nano detected, `192.168.7.1` routed via
Wi-Fi `en0`. Recheck after attachment. Resolve any remaining overlap explicitly
before testing; the smoke command must not alter host routes automatically.

After manually installing the preserved prototype and confirming the physical
board/address, close other source/echo clients and run:

```sh
.venv/bin/python tools/dev.py smoke --board nano_rp2040_connect --address 192.168.7.1 --allow-legacy-no-identity
```

The prototype cannot report stable identity. The mandatory legacy flag explicitly
acknowledges this temporary limitation; the harness must use verified identity
before commands are added to production firmware. The selected IP is not device
identity. The prototype also replaces an existing client on a second connection.

Smoke opens only source TCP 5000 and echo TCP 5001. It sends deterministic binary
test data, checks exact echoes and 272-byte source records, fragments writes,
reconnects, and combines a briefly stalled reader with source traffic. Each case
has an overall deadline; no reset/retry hides a failed connection. Output includes
verified byte counts, elapsed time and measured throughput, with first mismatch
offset and expected/observed bytes on corruption. No performance threshold is
claimed yet. A short reader pause does not prove long-term memory stability.

No flash, reset, host network configuration or destructive command is performed.
Without a selected board/address, direct `pytest tests/hardware` reports skipped.
Absent hardware is never a pass. Optional pytest reporting remains available:

```sh
.venv/bin/python -m pytest tests/hardware -v -s --board nano_rp2040_connect --address 192.168.7.1 --allow-legacy-no-identity --junitxml=test-results/smoke.xml
```

## iPhone/iPad manual record

Follow the staged [iPhone/iPad qualification procedure](ios-qualification.md).
Its browser page validates complete deterministic records; an increasing byte
counter alone is not sufficient. Begin with three distinct scenarios before any
20-connection release run.

For each target, record exact host model/OS, board revision, firmware hash, core
version, cable/hub, authorization state, elapsed time to lease, and successes /
attempts. Target 20 cold connections per available device for future USB/DHCP
changes. A connection requiring RESET or manual address changes fails automatic
startup even if recovery works. Current observations link to the preserved
[iPad qualification evidence](evidence/ios-qualification.md).

| Action | Expected result for production | Observed |
| --- | --- | --- |
| Cold plug-in | Stable attachment, automatic lease, first verified bytes without RESET | Pass 1/1 on the recorded iPad/local-only image; latency unmeasured |
| Previously authorized reconnect | Automatic lease and usable stream | Pass 1/1; the same host authorization prompt reappeared, then the exact stream resumed after Allow |
| First authorization, if reproducible | Prompt remains stable; acceptance allows traffic | Pass 1/1; exact prompt and screenshot preserved |
| Connect locked, then unlock | Normal authorization and automatic connection | Pass 1/1; Ethernet appeared automatically and the exact browser validator became green after unlock |
| Deny, disconnect, reconnect | No bypass; normal prompt/connection behavior | Untested |
| Reset and repeated unplug/replug | Recovery documented; count all startup failures | Untested |
| Stream while loading uncached Internet page via Wi-Fi | Board plus Internet both usable | Pass 1/1; external HTTPS loaded and the local exact-record count subsequently increased |
| Repeat via cellular on capable phone | Board plus Internet both usable | Untested |
| Inspect address, mask, router and DNS | Correct lease, absent DHCP router/DNS | Pass 1/1: `192.168.77.16/24`, empty Router and DNS Servers; historical baseline advertised both |

Do not routinely reset privacy settings to recreate first authorization. An LED
or Wi-Fi icon alone does not establish NCM binding, DHCP, routing or disassociation.
The baseline's original comments about its LED/startup are not new evidence.

For desktop diagnosis, identify the actual USB Ethernet interface, then optionally
capture `udp port 67 or udp port 68 or arp or tcp port 5000 or tcp port 5001` with
Wireshark/tcpdump on that interface. Capture requires appropriate permissions and
may include the test payload. Keep captures local unless intentionally sanitized
for sharing. Desktop captures do not observe the iPhone's USB exchange. Diagnose
power/reset, USB, NCM, DHCP/IP, TCP, then application bytes in that order.
