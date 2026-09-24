# Nano application TCP integration — 2026-09-18

Initial implementation checkpoint (before hardware runs below):
implementation/native/harness/build checks pass. **Not flashed at this checkpoint; real
FMGO TCP/lwIP/USB behavior and macOS/iOS interoperability remain untested.**
The existing flashed qualification image and paused 3/3 cold-cycle ledger remain
unchanged. No hardware command, reset or host-network modification ran here.

## Change and reason

Added `src/ports/arduino_pico/tcp_session.*`: two fixed placement-constructed
Sessions sharing one application Channel. Raw callbacks retain at most one pbuf
chain per socket, record EOF/faults and count TCP acknowledgments. Only application
poll parses protocol or calls/disposes the backend. All port calls, stop/destruction
and backend access require the Ethernet lwIP lock. Receive credit advances only
on decoder reads, and stops under queue saturation. TCP writes copy bounded
prefixes, retain unsent tails on ERR_MEM and never wait for socket space.

Two sockets allow channel BUSY to be encoded without displacing an owner; extra
sockets receive RST. Handshake time starts at acceptance, not delayed first poll.
Disconnect cleanup precedes other clients' polls. Terminal sessions release the
channel immediately and give copied output one additional bounded second for TCP
acknowledgment before FIN close or deadline abort. Successful close detaches all
callbacks/transfers PCB lifetime; ERR_MEM retries remain bounded. These are TCP
acknowledgments, not application completion. Pinned lwIP may RST refused/unread
input, so fatal errors remain best effort. No half-close or data replay.

Added the minimal `examples/nano_application/` reference on provisional TCP 7420:
local-only DHCP, final NCM-only descriptors and listener prepared before USB
attach, bounded 256-byte application loopback, stable lowercase board ID, one
random 64-bit boot nonce. NINA remains unused; no HTTP/source waits or legacy
services are in this sketch. No UART/reset/update/runtime capability advertised.

`dev.py build|flash|smoke --firmware application` explicitly selects it. Flash
retains ROM-volume, matching profile, successful build, source and image hash
gates. Existing default qualification flash/smoke behavior stays available.
Identity-checked FMGO smoke uses standard sockets/select and no automatic retry,
reset, upload or host networking edits. It tests exact fragmented full-duplex
bytes, delayed reads, BUSY, byte counters, unsupported reset, ownership transfer
and fresh reconnect. The expected reset result is rejection, not execution.
CI's declared compile matrix adds application; GitHub CI has not run here.

## Commands and actual results

```sh
.venv/bin/python tools/dev.py test --sanitize
ARDUINO_CONFIG_FILE="$PWD/arduino-cli.local.yaml" \
ARDUINO_CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' \
.venv/bin/python tools/dev.py build --board nano_rp2040_connect --firmware application
ARDUINO_CONFIG_FILE="$PWD/arduino-cli.local.yaml" \
ARDUINO_CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' \
.venv/bin/python tools/dev.py build --board nano_rp2040_connect --firmware local-only
.venv/bin/python tools/dev.py build --help
.venv/bin/python tools/dev.py flash --help
.venv/bin/python tools/dev.py smoke --help
```

Final regression run: **8/8 CTest suites, 70 Unity cases, ASan/UBSan enabled**;
**79 pytest host cases pass**. CTest 1.21 s, host tests 0.92 s. Ten new raw-adapter
cases test chained pbuf input, exact binary bytes under 7-byte writes/ERR_MEM,
coalesced frames/stopped receive credit with a stalled peer, explicit second-client
BUSY/extra-socket refusal, EOF/error callbacks deferring cleanup, reconnect without
replay, acknowledged fatal replies, close-memory recovery/deadlines, clock rollover
and listener failure/stop. Three new tests compile actual reference setup/identity
function bodies to verify service-before-USB ordering, all startup failure paths
remaining detached, uppercase SDK board-ID normalization and boot nonce formatting.
The nonce test uses a fake changing RNG and cannot establish hardware entropy.

Host cases add malformed/oversized/wrong-ID header rejection, exact fragmented/
coalesced echo with a local simulated peer, wrong identity before any channel
command, corrupt bytes/wrong channel/EOF/timeouts, and flash gates on both profiles.
These simulated peers validate the harness, **not firmware**. Initial host sockets
were blocked by the sandbox (six PermissionError failures); final runs with
localhost listeners permitted passed. No missing hardware is counted as passed.

Build uses CLI 1.5.1, Arduino-Pico 6.0.0 and the same pinned deferred NCM-only core
overlay/FQBN/layout/memory budgets. Reports contain exact commands, source hashes,
patch hashes, exported artifact hashes and size results:

| Profile | Program | Static RAM | UF2 bytes | Report |
| --- | ---: | ---: | ---: | --- |
| application | 116,940 | 86,560 | 269,312 | [snapshot](tcp-session-application-build.json) |
| local-only qualification | 116,604 | 76,808 | 268,288 | [snapshot](tcp-session-local-only-build.json) |

Both are within 1 MiB program/192 KiB static RAM budgets. Final incremental logs
contain six upstream WiFiClient overloaded-virtual warnings each, no project
warnings. The first full application compile had 22 instances of those upstream
warnings; the unchanged library cache reduced repeats on final rebuild.

Application UF2 SHA-256:
`6e17414409cf085ef092a87c1e227953fac0d84cb99e7cf24b1d467961787441`.
Qualification UF2 SHA-256:
`dd6430cc4e15ff7d44f1cc2e553b11d20e4102855628963816c7470bb9cb2e5b`.
Neither image was flashed. Build source/image hashes were checked against current
files after final builds. Preserved baseline SHA-256 remains
`4887fd480e98025392a85751138127410ad0b314a6ea51c7f18bdd1d1cc3c209`.

Pinned ELF symbol inspection:

```sh
.arduino-data/packages/rp2040/tools/pqt-gcc/5.0.0-9576866/bin/arm-none-eabi-nm \
-S -C build/firmware/nano_rp2040_connect/application/artifacts/nano_application.ino.elf
```

Reference objects: TCP server (including both Session slots) 9,784 bytes,
Channel 568, Loopback 268, Identity 100; total 10,720 bytes. These are static ARM
object sizes, **not runtime stack/heap high-water measurements**. The pinned
IPv4-only lwIP configuration independently bounds TCP receive/send windows at
11,680 bytes each, 16 KiB lwIP heap, five TCP PCBs, 32 TCP segments and 24 pool
pbufs. Real-stack resource contention/TIME_WAIT behavior needs hardware checks.

Local logs: `build/tcp-session-regressions.log`,
`build/application-firmware-build.log`, `build/application-qualification-build.log`
and corresponding firmware `build.log` files. Build/run host: MacBookAir10,1,
macOS 27.0 (26A428), current pinned venv/native toolchain. No new iOS observations.

## Next hardware check

Use [Nano ROM recovery](../usb-startup.md), explicitly flash application, then
run [application smoke](../testing.md#application-protocol-smoke) and independent
actual interface/router/DNS/default-route/uncached-Internet checks. Do not use
qualification HTTP smoke on the application sketch. Record exact board/cable/OS,
image hash and observations. No new physical qualification cycles are armed;
repetition remains paused by user request. Native fakes/build success do not
qualify this firmware for release or replace real iPhone/iPad authorization tests.

Subsequent flash, failures and timer-service correction are recorded separately
in [application hardware evidence](application-hardware.md). Initial build/test
results above remain historical; use the later report for current image state.
