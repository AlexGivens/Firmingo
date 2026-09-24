# Nano UART backend evidence — 2026-09-22

## Claim boundary

The portable hardware-UART backend, Arduino-Pico adapter, Nano reference image,
protocol schemas, and host smoke harness are implemented. Native/sanitizer, host,
bounded mutation, and pinned firmware build checks pass. `uart-dev-v1` remains
preserved as the failing predecessor: its bounded macOS smoke/coexistence check
passed, but its first measured soak exposed UART RX loss. The focused
`uart-dev-v2` correction is now flashed and passes smoke plus the same unretried
soak parameters with exercised throttling and no UART loss. The independent v2
Pico fixture also passes all advertised UART formats and bounded baud endpoints;
its separate 2 Mbaud burst failure defines a no-CTS buffering limit.
`application-dev-v7` remains compile-tested only.

## Implemented behavior

- Channel 1 can describe either an application or UART backend without importing
  Arduino, Pico SDK, TinyUSB, or lwIP types into portable core code.
- UART configuration is atomic and explicit: baud 300–2,000,000, 5–8 data bits,
  none/even/odd parity, one or two stop bits, and `flow_control:"none"` only.
  Replies distinguish requested and actual baud.
- The Nano port uses `Serial1`/RP2040 `uart0`: D1/TX is GPIO 0 and D0/RX is GPIO 1.
  It performs only currently possible reads/writes and checks the pinned SDK's
  UART busy register instead of waiting for hardware.
- A configuration request waits for empty portable queues and inactive UART TX.
  Successful reconfiguration drains and counts unread RX first. New ownership is
  also held off until old hardware TX drains, preventing delayed bytes from an old
  owner entering a new session.
- RX storage is bounded: Arduino-Pico FIFO 256 bytes and portable Stream queues
  256 bytes per direction. With no CTS, a remote sender can overrun RX; diagnostics
  expose event count and a conservative minimum lost-byte count.
- V2 retains a board-declared 64-byte RX reserve before accepting additional
  locally originated TX. When an echoing/responding target fills RX while network
  output is backpressured, UART writes return zero into the existing bounded
  Stream queue. `uart_tx_throttles` is a saturating boot-lifetime count of those
  deferred write attempts. This cannot stop unsolicited external UART input.
- UART diagnostics use `uart_rx_pending`, `uart_rx_discarded`,
  `uart_rx_overrun_events`, `uart_rx_lost_bytes_minimum`, and
  `uart_tx_throttles`. The maximum UART-plus-transport reply is 776 payload bytes.
  A smaller negotiated limit gets
  the explicit `response_too_large` error.
- The UART host smoke requires a physical D1-to-D0 loopback. It checks identity,
  advertised capability, second-client BUSY, 16 KiB exact loopback at 115200 8N1,
  acknowledged reconfiguration, 4 KiB delayed-reader loopback at 57600 8N1,
  exact 20,480-byte counters, and zero observed overrun/loss counters. It does not
  flash, reset, retry, or alter host routes.

## Board-free verification

Commands run from the repository root:

```sh
.venv/bin/python tools/dev.py test --sanitize
.venv/bin/python -m pytest tests/host -q --junitxml=build/uart-format-host.xml
.venv/bin/cmake -S . -B build/protocol-fuzz -DFIRMINGO_SANITIZE=ON -DFIRMINGO_PROTOCOL_FUZZ=ON
.venv/bin/cmake --build build/protocol-fuzz --parallel 2
.venv/bin/ctest --test-dir build/protocol-fuzz -R '(protocol|session)_fuzz' --output-on-failure
build/protocol-fuzz/protocol_fuzz
build/protocol-fuzz/session_fuzz
```

Results:

- All 10 sanitizer-backed native CTest suites passed, including `uart` and
  `session`.
- All 153 host tests passed; 20 directly cover UART smoke/current-state and soak
  validation, and 19 cover the peer fixture and format-matrix generator, parsing,
  coverage, and effective-configuration checks. A sandboxed combined run blocked
  18 pre-existing localhost socket cases with `EPERM`; rerunning the host suite
  with local-loopback access passed all cases.
- Protocol mutation: 20,000 cases, seed `464d4750`, pass.
- JSON/session mutation: 20,000 cases, seed `53455331`, pass.

Preserved v2 outputs: [native log](uart-v2-native.log),
[current 153-case host JUnit](uart-format-host.xml), the earlier
[140-case host JUnit](uart-peer-host.xml), [134-case host JUnit](uart-v2-host.xml),
and [mutation log](uart-v2-fuzz.log).

## Pinned firmware builds

Both builds used Arduino CLI 1.5.1, Arduino-Pico 6.0.0, Pico SDK USB stack,
NCM-only USB, the existing deferred-start overlay, IPv4-only lwIP, 125 MHz,
small optimization, disabled RTTI/exceptions, and the 16 MB no-filesystem layout.
The source manifest now excludes hidden operating-system metadata so ignored
`.DS_Store` files cannot change verification identity.

| Image | Flash | Static RAM | UF2 SHA-256 | Status |
| --- | ---: | ---: | --- | --- |
| `uart-dev-v1` | 126,488 bytes | 87,336 bytes | `6023094748ba44bb62f33815b2d079d7eef21ded0cbb13fcb961d847ec0a1dc5` | Initial smoke passed; first soak failed |
| `uart-dev-v2` | 126,664 bytes | 87,408 bytes | `84386e112ab4b8909672bf7299c125a816a157b36a0dec644d2dfb7ae5aa4e5a` | Hardware-tested on one Nano/macOS host |
| `application-dev-v7` | 125,968 bytes | 88,192 bytes | `8ae2711328330365dc5afac4034106a05344dc6cb05fe6bb699fa24d60da5e63` | Regression compile-tested only |

Each build remains within the board's 1 MiB image and 192 KiB static-RAM
engineering caps. Each emitted six instances of the pinned upstream
`WiFiClient::write(uint8_t)` hidden-overload warning; no project-source warning
was emitted. Preserved reports and logs:

- [UART v1 report](uart-v1-build.json) and [build log](uart-v1-firmware.log)
- [UART v2 report](uart-v2-build.json) and [build log](uart-v2-firmware.log)
- [Application v7 report](application-v7-build.json) and
  [build log](application-v7-firmware.log)

The independent test fixture is an original Raspberry Pi Pico using the same
pinned Arduino CLI/core but a normal USB-CDC plus UART0 profile. Current
`pico-uart-peer-v2` adds atomic data/parity/stop configuration and compiles to
60,004 flash bytes and 9,320 static RAM bytes. Its 153,600-byte UF2 SHA-256 is
`e04956c640f4bfc27a005607b065d1bf660209f47906e64e16d2a6bd936949d0`.
Preserved [current fixture build report](pico-uart-peer-build.json) and
[build log](pico-uart-peer-firmware.log) establish compilation only; the tests
below establish its observed fixture behavior. The original v1
[report](pico-uart-peer-v1-build.json) and [log](pico-uart-peer-v1-firmware.log)
remain with the first peer run.

## Hardware loopback and Internet coexistence

The report-verified UART UF2 was copied to the explicitly selected `RPI-RP2`
ROM volume with D1/TX jumpered to D0/RX. Test target: Arduino Nano RP2040 Connect,
device `a1b2c3d4e5f60718`, boot `0f42d9d229d4bd01`; host `MacBookAir10,1`, macOS
27.0 build 26A428. Cable/hub details and authorization-prompt observation were
not recorded. No RESET, route change, or automatic retry was used.

The first smoke invocation passed:

- UART hello reported 115200 requested / 115207 actual, 8N1, no flow control.
- Second-client ownership returned BUSY.
- 16,384 exact bytes looped at 115200 in 1.689 s (9,702 B/s).
- Reconfiguration to 57600 was acknowledged; actual baud was 57597.
- 4,096 exact bytes looped with a delayed reader in 1.118 s (3,665 B/s).
- Status reported exactly 20,480 RX and 20,480 TX bytes, with both portable
  queues empty. UART overrun events and minimum lost bytes were zero.

A repeat invocation initially stopped before stream traffic because the host
harness incorrectly required the boot default after the prior session had
persisted 57600. The firmware correctly advertised its current configuration.
The harness now validates any supported persisted state and supplies 115200
explicitly on open. Eighteen focused host cases and the then-complete 132-case
host suite passed after this fix. A repeat smoke starting from 57600 then passed, proving
the check is rerunnable without resetting the board.

The final smoke also passed while a delayed fresh IPv4 HTTPS request completed
with status 200 against remote `23.62.133.47`. During that run, 16,384 exact
115200 bytes completed in 1.712 s (9,571 B/s), followed by 4,096 exact delayed-
reader bytes at 57600 in 1.112 s (3,682 B/s); counters remained exact and UART
overrun/loss remained zero.

macOS DHCP inspection showed `192.168.77.16/24`, server `192.168.77.1`, a
600-second lease, and no router or DNS option. The board route used USB Ethernet
`en5`; the default route to `1.1.1.1` remained Wi-Fi `en0` through
`192.168.4.1`. This establishes one bounded macOS loopback and Internet-
coexistence pass, but the subsequent soak supersedes any sustained-reliability
inference from that short check.

## Preserved UART v1 soak failure

The new UART-aware soak command was run once for a requested 60 seconds using
16,384-byte seeded batches, alternating delayed readers, reconnect every four
batches, explicit 115200 configuration on each open, and required diagnostics.
It performed no reset or retry.

Twenty-four batches and six sessions completed with 393,216 exact bytes. Batch 25
then timed out after returning 15,570/16,384 bytes even though all 16,544 framed
wire bytes had been sent. Elapsed time was 70.976 seconds because the failing
batch consumed its 20-second deadline. The last successful diagnostics showed
unchanged sampled heap/stack minima, zero UART overrun/loss, no TCP errors, and
empty queues. A post-failure snapshot on the same boot then showed:

- `peak_to_peer:256`
- `uart_rx_overrun_events:62`
- `uart_rx_lost_bytes_minimum:62`
- `tcp_errors:0`
- unchanged `heap_min:174264` and `stack_min:8064`

The observed byte deficit was 814; the hardware API only latches overrun state,
so 62 is a conservative minimum rather than an exact loss count. The production
Stream continued accepting host-to-UART data while its UART-to-host queue was full.
The D1-to-D0 loopback therefore kept adding local TX bytes to an already saturated
RX path. This identifies a UART-flow defect independently of the earlier NCM-worker
hypothesis. Preserved raw evidence: [failed soak](uart-v1-soak-failure.json) and
[post-failure snapshot](uart-v1-post-failure.json).

UART v2 makes the driver's bounded RX capacity/reserve explicit, stops accepting
local TX at 192/256 queued RX bytes, retains pending host bytes in the existing
Stream queue, and exposes throttle attempts. Native tests cover the threshold,
partial progress, persistent overrun/loss, and throttle count. Its larger worst-
case diagnostics response is covered by the 800-byte payload buffer and explicit
`response_too_large` behavior for smaller negotiation.

## UART v2 hardware replay

The report-verified v2 UF2 was copied to the explicitly selected `RPI-RP2`
volume with the D1-to-D0 loopback retained. It booted as `uart-dev-v2`, device
`a1b2c3d4e5f60718`, boot `44c3822e1f67a355`. No reset or retry was used.

The identity-checked smoke passed on its first attempt:

- Second-client acquisition returned BUSY.
- 16,384 exact bytes looped at requested 115200/actual 115207 baud.
- Reconfiguration to requested 57600/actual 57597 baud succeeded.
- 4,096 exact delayed-reader bytes looped at 57600.
- Status reported exactly 20,480 bytes in each direction, empty portable queues,
  and zero UART overrun/minimum lost-byte counts.

The exact v1 soak parameters were then replayed once: requested 60 seconds,
16,384-byte seeded batches, alternating delayed readers, reconnect every four
batches, 20-second per-batch deadline, and required diagnostics. It completed in
60.952 seconds with 28/28 batches, seven closed sessions, and 458,752 exact bytes.
Batch 25, where v1 had failed, completed in 1.711 seconds. No reset, retry, or
failure rescue occurred.

The fix was exercised. `uart_tx_throttles` was already 1,277 after smoke and rose
to 3,521 during the soak. Across all samples, UART overrun events, minimum lost
bytes, discarded RX bytes, and TCP errors remained zero. Final queues were empty;
peak portable queues were 256 bytes toward the backend and 213 toward the peer.
Sampled heap/free minimum stayed at 174,192 bytes and approximate core-0 stack
minimum stayed at 8,064 bytes. These samples do not establish exhaustive memory
stability or cover unsolicited external UART input.

During the soak, a fresh IPv4 HTTPS request completed with status 200 against
remote `172.66.147.243`; another completed after the soak. The local board route
remained USB Ethernet `en5`, and the default route remained Wi-Fi `en0` through
`192.168.4.1`. DHCP assigned `192.168.77.16/24` with a 600-second lease and no
router or DNS option. Preserved raw evidence: [v2 soak](uart-v2-soak.json).

## Independent Raspberry Pi Pico peer

An original Raspberry Pi Pico was flashed with the report-verified peer fixture.
Both boards used their own USB power/data cable. Nano TX/GPIO0 connected to Pico
GP1/UART0 RX, Nano RX/GPIO1 connected to Pico GP0/UART0 TX, and ground was shared;
no power rails were connected. The Pico control port enumerated as
`/dev/cu.usbmodem1101` and identified itself as `pico-uart-peer-v1`.

The first hardware harness invocation proved 8,192 exact bytes Nano-to-Pico and
8,192 exact bytes Pico-to-Nano at requested 115200 baud. It then stopped before
57600 traffic because a type-3 UART frame preceded the expected configure reply.
Captured state explained the frame: reinitializing each UART while the other
receiver remained active produced one transition byte. Pico showed 8,193 RX bytes
after the 8,192 measured bytes; Nano had discarded one peer transition byte. Both
overrun/loss counts remained zero. The aborted TCP owner increased `tcp_errors`
once. This was a harness sequencing failure, not a passing full run.

The harness was corrected to use separate ownership sessions per baud. It changes
the peer baud while the Nano has no owner, lets acquisition discard transition
noise, and clears the peer only after the Nano UART starts. The corrected complete
run passed:

- 8,192 deterministic exact bytes Nano TX to Pico RX at 115200.
- 8,192 deterministic exact bytes Pico TX to Nano RX at 115200.
- 4,096 deterministic exact bytes Nano TX to Pico RX at 57600.
- 4,096 deterministic exact bytes Pico TX to Nano RX at 57600.
- Empty Nano queues and exact per-session counters.
- Pico overrun and mismatch counts stayed zero; Nano UART overrun and minimum
  lost-byte counts stayed zero during the measured directional run.

Post-run sampled memory remained at a 174,192-byte heap minimum and 8,064-byte
core-0 stack minimum. The boot-lifetime Nano diagnostics retained three discarded
transition bytes and the one TCP error from the earlier harness stop. Preserved
[post-run peer/Nano snapshot](uart-v2-peer-post.json).

## Controlled remote-input overrun and recovery

With no session owning the Nano UART, the Pico transmitted one 4,096-byte burst
at 57600. Before the burst, RX pending/discarded/overrun/minimum-loss were
`0/3/0/0`. Afterward, the bounded 256-byte RX FIFO was full and diagnostics were
`256/3/1/1`. The minimum loss value is intentionally conservative: it proves at
least one byte was lost but does not claim the true burst deficit.

The next owner acquisition drained exactly 256 stale queued bytes. Diagnostics
became `0/259/1/1`, preserving the overrun and minimum-loss evidence. Without a
reset, the same session then transferred 4,096 deterministic exact bytes in each
direction. Both portable queues ended empty, Pico reported zero mismatch/overrun,
and Nano discard/overrun/minimum-loss/TCP-error counters did not change during
recovery. The default Internet route remained Wi-Fi `en0`. Preserved raw
[overrun/recovery report](uart-v2-overrun.json).

## UART format matrix and high-rate boundary

The verified `pico-uart-peer-v2` image was flashed to the original Pico. With
both boards powered independently and the same crossed TX/RX/shared-ground
wiring, the matrix checked the Nano on fresh boot `f9416aab7d9e57de` without
automatic retries or resets.

The passing run covered all 24 combinations of 5–8 data bits, none/even/odd
parity, and one/two stop bits at 57600. It also covered 8N1 at the advertised
300-baud minimum and 2,000,000-baud maximum. Lower-width deterministic values
were masked to the configured data width. Every case transferred exact bytes in
both directions: 256 bytes per format, 32 at 300 baud, and 128 at 2 Mbaud, for
6,304 exact bytes each way across 26 sessions. Seven invalid configurations were
rejected atomically by each device. Nano overrun, minimum-loss, and TCP-error
counters stayed zero; sampled heap/stack minima remained 174,192/8,064 bytes.
Reconfiguration discarded 18 line-transition bytes before ownership, as designed.
Preserved [passing matrix](uart-format-matrix.json).

Two stopped attempts remain visible. The first sent no UART data because the new
harness incorrectly expected `serial.open` to return configuration fields; it now
reads those fields from a second `hello`, per the protocol. Preserved
[harness failure](uart-format-harness-failure.json). The next run passed the
300-baud case and all 24 formats, then sent an uninterrupted 1,024-byte 2 Mbaud
burst. Pico received and transmitted all 1,024 generated bytes with no peer-side
mismatch/overrun, while Nano diagnostics subsequently reported three RX overruns
and three conservative minimum lost bytes; the Nano-to-host read timed out. This
is a real no-CTS buffering limit, not a passing maximum-throughput claim.
Preserved [failed burst run](uart-format-2mbaud-burst-failure.json) and
[post-failure observation](uart-format-2mbaud-post.json).

## Next hardware evidence

Physical UART evidence now covers every advertised data/parity/stop-bit shape,
bounded checks at both baud limits, longer 8N1 traffic at 57600/115200, sustained
loopback, and explicit overrun/recovery. It does not establish arbitrary burst
tolerance or sustained throughput at 2 Mbaud without flow control. Cold-attachment
rates and iOS remain separate open evidence. Because intentional overrun checks
leave boot-lifetime loss counters nonzero, ordinary zero-loss smoke or soak should
use a deliberate fresh Nano boot rather than hiding those counters.
