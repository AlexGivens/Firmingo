# Application protocol hardware debugging — 2026-09-18

Status: **application v7 flashed; full macOS smoke and a bounded diagnostic
traffic run with concurrent Internet access pass**. Earlier failed reconnect and
larger-transfer runs remain failures and are retained below. Physical cold qualification
remains paused at 3/3; these are protocol-development checks, not new cold cycles.

Host: MacBookAir10,1, macOS 27.0 (26A428). Board: Nano RP2040 Connect, device ID
`a1b2c3d4e5f60718`, USB NCM on en5, `192.168.77.1`; host address `.16/24`.
Board revision/cable/hub and authorization observations not recorded for these
flashes. NINA unused; no reset command executed or host network setting changed.

## Flash and observed protocol results

Each flash used explicitly prepared `/Volumes/RPI-RP2`, `INFO_UF2.TXT` identified
Board-ID RPI-RP2, and successful-build/current-source/image hash gates:

```sh
.venv/bin/python tools/dev.py flash --board nano_rp2040_connect --firmware application --mount /Volumes/RPI-RP2
.venv/bin/python tools/dev.py smoke --board nano_rp2040_connect --firmware application --address 192.168.77.1 --device-id a1b2c3d4e5f60718
```

| Image | Boot ID observed | Results |
| --- | --- | --- |
| Initial integration UF2 `6e17414409cf085ef092a87c1e227953fac0d84cb99e7cf24b1d467961787441` | `90bc40f5e54153be` | Verified hello; second-client BUSY; exact 16,384-byte echo, 4,096-byte delayed-reader echo, counters/unsupported reset, ownership transfer and exact 4,096-byte echo; final fresh reconnect reset |
| Slot replacement/null-PCB guard UF2 `b23aa2c422e3508e58c47e06a6b03b6318904d9d9d0f7f6456b56bb57f169d7e` | `70b40924f964be7a` | Same checks pass; final fresh reconnect still reset. Waiting for peer TCP closures in the revised harness also failed after stream/transfer traffic |
| Timer-service UF2 `5672a839ae6ddd558aca846bd688441decd7c15ccef47230851eebef31fb02a5` | `d362b10ba8d61658` | Full single-pass smoke succeeds, including fresh 4,096-byte reconnect echo; separate 65,536-byte exact transfer and concurrent HTTPS succeed |
| Application v7 UF2 `8ae2711328330365dc5afac4034106a05344dc6cb05fe6bb699fa24d60da5e63` | `32933ceacb2849ea` | Full single-pass smoke succeeds; 180,224-byte diagnostic traffic run across 11 batches/6 sessions succeeds while a concurrently launched fresh HTTPS request returns 200 |

[Initial smoke](application-first-smoke.log),
[slot-fix smoke](application-race-fix-smoke.log),
[confirmed-close smoke](application-confirmed-close-smoke.log).
Each failed full run verified 24,576 exact application bytes before failure; planned
fresh-session 4,096-byte echo did not complete. No whole-suite pass at that checkpoint.
Slot-fix run measured 16 KiB echo at 20,400.9 bytes/s and transfer echo at
1,756.2 bytes/s; these isolated measurements are not a sustained-throughput claim.
Counters after the first two echoes were RX=TX=20,480, pending queues both zero.
Unsupported reset returned `unsupported`; firmware was not reset by that request.

A separate focused 64 KiB echo with concurrent HTTPS attempted on the initial
image timed out at 15 s after receiving 63,488/65,536 bytes, with the entire
66,176-byte framed input sent. Partial data was not verified by the checker
because its full-length comparison was never reached. HTTPS result was not
printed when the echo future raised, so that run supplies **no recorded
concurrent Internet pass**. See [diagnostic](application-large-transfer-diagnostic.log).

A minimal two-session close diagnostic with no stream traffic observed peer RST
for both sockets, then passed fresh hello/1,024-byte echo without sleep/retries:
[log](application-confirmed-close-diagnostic.log). This suggested close timing,
but the later full stream smoke still failed with confirmed closure. It does not
establish a general reconnect fix or the precise reset cause.

## Source findings and corrections

1. An accept can follow EOF/error before the application loop disposes the old
   Session. Native regression initially returned ERR_ABRT instead of accepting
   the already-dead socket's replacement. The port now reserves that same slot,
   refuses new input until old Session/pbuf/backend queues are disposed in poll,
   then constructs a fresh Session with acceptance-time deadline. It retains
   exactly two live PCB slots and never runs backend work in callbacks.
2. Pinned lwIP `tcp_in.c` reports listen allocation exhaustion through the accept
   callback with NULL PCB/ERR_MEM. The old handler attempted `tcp_abort(NULL)`.
   Native regression caught that invalid access. The guard returns the supplied
   error without aborting a null pointer. This was a source/native finding, not
   an observed hardware reset. Both regressions failed before the fix:
   [before-fix output](application-native-before-fix.log).
3. Arduino-Pico 6.0.0 `libraries/lwIP_Ethernet/src/LwipEthernet.cpp` explicitly
   documents that frequent lwIP lock releases reschedule its 20 ms background
   timeout worker, postponing `sys_check_timeouts()`. Our minimal raw-only loop
   polls under that lock every millisecond. Unlike high-level socket paths, it
   did not dispatch lwIP timers explicitly. Refused-data retry (tcp_fasttmr),
   delayed ACK and TCP retransmission therefore cannot rely on the postponed
   background worker. The raw port now calls `sys_check_timeouts()` once at the
   beginning of each application poll under the lock. Transport faults from
   timers are disposed before other session work. A production-adapter test
   queues stack-owned refused hello data and verifies timer-driven delivery,
   negotiation and one timer-dispatch call per poll.

Timer starvation is a concrete missing service in source. After adding explicit
timer dispatch, the previously failing full smoke and larger transfer both pass
on this Mac. This supports the correction; it does not identify the precise
retained packet/reset sequence without a trace. Packet
capture was unavailable: ordinary tcpdump lacked BPF permission; noninteractive
sudo required a password. No packet trace/payload capture was produced and no
privilege/privacy setting was changed.

The harness now explicitly half-closes its sending side and observes peer EOF or
RST before declaring remote TCP closure. For this firmware EOF causes an abort,
so RST is a normal close event. There must be no pending data/control response.
Unread data or a three-second bounded close timeout fails; a refused fresh
connection is never retried. `serial.close` releases the channel, not the TCP
socket; local descriptor close alone does not establish remote closure. Five
focused host cases cover EOF/RST, timeout, trailing data and idempotent finish.
This harness change did not turn the retained full smoke failure into a pass.

## Internet and routing observations

[Recorded route/DHCP/Internet output](application-internet-check.log): default
IPv4 route through Wi-Fi en0 via `192.168.4.1`; board `/24` route through en5;
DHCP ACK `.16`, server `.1`, subnet `/24`, lease 600/T1 300/T2 525 seconds,
**no router option 3 or DNS option 6**. A fresh numeric-query HTTPS request to
example.com returned HTTP 200, remote `104.20.23.154`, while the application board
was attached. This confirms current Internet access/route coexistence; it is
not a completed byte-stream-plus-Internet concurrency test or iOS evidence.

Read-only commands:

```sh
route -n get default
route -n get 192.168.77.1
ipconfig getpacket en5
curl -4 --fail --silent --show-error --max-time 10 --output /dev/null --write-out '%{http_code} %{remote_ip}\n' 'https://example.com/?firmingo_application=<fresh-time>'
```

## Current software verification and next action

```sh
.venv/bin/python tools/dev.py test --sanitize
ARDUINO_CONFIG_FILE="$PWD/arduino-cli.local.yaml" \
ARDUINO_CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' \
.venv/bin/python tools/dev.py build --board nano_rp2040_connect --firmware application
ARDUINO_CONFIG_FILE="$PWD/arduino-cli.local.yaml" \
ARDUINO_CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' \
.venv/bin/python tools/dev.py build --board nano_rp2040_connect --firmware local-only
```

Final ASan/UBSan run passes 8/8 CTest suites, **73 Unity cases** and **84 host
pytest cases**, 0.96 s native/0.99 s host. Three added adapter regressions cover
slot reuse with old queued data/no replay/no new-PCB receive credit for old bytes,
null-PCB allocation failure, and explicit timer dispatch/refused delivery.
Fake timer/stack assertions do not test real lwIP timing; pinned compile is separate.

Timer-service application build: program 117,036 bytes, static RAM 86,560 bytes,
UF2 269,824 bytes, pinned CLI 1.5.1/core 6.0.0/NCM-only overlay. Six upstream
WiFiClient overloaded-virtual warnings, no remaining project warnings. Image and
source hashes checked current; [build snapshot](application-timers-build.json).
Qualification fallback also rebuilds: program 116,604, static RAM 76,808, unchanged
268,288-byte UF2 (hash `dd6430cc4e15ff7d44f1cc2e553b11d20e4102855628963816c7470bb9cb2e5b`).
No baseline change, updater, UART port or iOS qualification was added.

The requested ROM flash and hardware checks completed as recorded below.
There is no CDC/software upload/reset channel; future replacement still uses
[ROM recovery](../usb-startup.md). The Nano now runs the timer-service image.
No new cold-attachment repetitions are armed.


## Passing timer-service hardware check — 2026-09-18

With RPI-RP2 prepared by the user, `dev.py flash --firmware application` copied
the verified timer-service UF2. The next `dev.py smoke --firmware application`
ran **once, without reset/retry**, and returned exit 0:
[complete log](application-timers-smoke.log).

| Check | Observed |
| --- | --- |
| Hello/identity | Expected device/board/firmware/capabilities; boot `d362b10ba8d61658` |
| Second owner | Explicit BUSY; existing owner retained |
| Fragmented full-duplex echo | 16,384 exact bytes, 0.989593 s, 16,556.3 bytes/s |
| Delayed-reader echo | 4,096 exact bytes, 0.751300 s |
| Status | RX=TX=20,480; both pending queues zero |
| Device reset request | Unsupported result; no reset executed |
| Ownership transfer | 4,096 exact bytes, 0.728441 s |
| Confirmed peer closure/fresh reconnect | 4,096 exact bytes, 0.726970 s |

Total smoke: **28,672 exact bytes** over the four echo checks. The tiny/fragmented
transfer rates include protocol/TCP waiting and are not peak USB throughput.

A separate identity-checked 64 KiB delayed-reader transfer ran concurrently with
one fresh IPv4 HTTPS request to example.com. Result:
[JSON timings/identity/counts](application-timers-coexistence.json),
[exact experiment script](application-coexistence-check.py).
The script was executed as `PYTHONPATH="$PWD" .venv/bin/python -u
build/application-coexistence-check.py` from the repository root. It selects this
specific board and uses no reset/retry/network edits; it is an evidence experiment,
not a new user-facing host product or soak suite.

- Exact echoed bytes: **65,536**, 500 fragmented sends, 5.999963 s,
  **10,922.7 bytes/s**. The formerly missing final 2 KiB now arrives.
- Backend status: RX=TX=65,536, pending queues zero.
- HTTPS: exit 0, HTTP 200, remote `104.20.23.154`, 0.380465 s. Monotonic interval
  `341853.622631291..341854.003096750` is fully contained within the transfer
  `341853.617651458..341859.622391500`.
- UTC evidence interval: `2026-09-18T21:59:23Z..2026-09-18T21:59:29Z`.
- [Post-test route/DHCP log](application-timers-network.log): default through en0/`192.168.4.1`, board through en5.
  DHCP ACK `.16/24`, server `.1`, lease 600/T1 300/T2 525, no router/DNS options.

Across smoke plus this separate check: **94,208 exact application bytes**; this
is bounded development evidence, not a memory-stability soak or release qualification.
No first-authorization, locked/denied iOS connection, iOS automatic lease/Internet
routing, new cold-cycle count, board revision/cable/hub observation or runtime
memory high-water measurement was supplied. Those remain open. Next development
work should integrate a real application backend/explicit UART capabilities and
add opt-in sustained-traffic/memory evidence; iPhone/iPad checks require real devices.

Documentation-update rebuild produced the identical tested UF2 hash; no further
flash was needed. [At-flash build metadata](application-timers-flashed-build.json)
and [current source/build metadata](application-timers-build.json) are both retained.


## Diagnostics image follow-up

The current Nano image is the diagnostics build
`f553b19b18b2b265e042c38ef33cead60dd0c01244caa48c001c00b0c21850d4`.
It passes full smoke, a two-minute required-diagnostics soak (1,245,184 exact
bytes, 76 batches, 19 TCP sessions), and a separate 64 KiB exact transfer with
fresh Internet access contained in the transfer interval. Sampled C heap and
approximate core-0 stack values remain stable; lwIP pool/deepest stack and iOS
remain unmeasured. See [full evidence and limits](application-diagnostics.md).
Physical cold-attachment qualification remains paused at 3/3.

## V2 application endpoint

`application-dev-v2` is now flashed. Full smoke and a 1,310,720-byte measured
soak pass with stable sampled memory, bounded application queues and no discarded
bytes. Isolated 64 KiB and a two-second reader pause also pass. Three unassisted
64 KiB transfers overlapping fresh Wi-Fi Internet requests stalled; HTTPS itself
succeeded, and failed-session cleanup discarded one full queue per direction
without replay. V2 is therefore not coexistence-qualified. See the complete
[v2 attempt ledger](application-backend.md).

## V3 failure and v4 NCM receive continuation

The fourth unassisted overlap failure was captured on `en5`. The Nano stopped
advancing its TCP receive ACK while retaining a nonzero receive window and running
outbound retransmission timers. The pinned Arduino-Pico 6.0.0 NCM worker handles
at most ten receive renewals per run but did not schedule another run when packet
10 succeeded. V3 preserved that bound and directly marked the worker pending. It
flashed and passed full smoke, but its first isolated 64 KiB replay failed before
any Internet request, again returning only 6,656 bytes. The SDK async context
clears pending before calling a worker; a callback-side bit assignment does not
wake its IRQ.

V4 calls `async_context_set_work_pending()` after releasing the USB mutex. The
actual-worker regression drains an 11-packet burst as runs of 10 and 1, verifies
one wake request, and verifies the request is outside the mutex. V4 also reports
persistent NCM and raw-TCP counters through diagnostics. UF2 SHA-256
`155521c96f77720ad7abdbfb0d3423a2f41f24b95ebfa87e4a683a48c05e91ba` was
hash-gated and flashed. Full smoke, one isolated 64 KiB replay, one overlapping
IPv4 Internet replay, and a 1.25 MiB two-minute diagnostic soak pass. Routes,
sampled heap/stack, queues, discards and TCP errors remained stable. The NCM
budget/wake counters stayed zero, so hardware did not directly exercise the new
continuation branch. See the [capture and replay analysis](application-backend.md).

A refined two-connection stress case then completed 64 recoverable control
exchanges alongside an exact 65,536-byte owner stream. It recorded 440 NCM frames
over 406 worker runs and still no budget/wake event. Two earlier large-frame harness
shapes closed the observer connection and are retained as failures. A worker-run
batch high-water counter is the next focused measurement.

`application-dev-v5` implements that persistent `ncm_rx_batch_peak` counter. Its
native actual-worker regression records a peak of 10 for an 11-packet synthetic
burst and retains that peak during the continuation run. The pinned firmware build
passes at 121,020 B flash and 87,808 B static RAM, UF2 SHA-256
`7665d4fc0c23494e8a1954ec95541d64a1ff8d4960a7219d33ba3e3664ae2192`.
V5 is not yet flashed; none of those results are new hardware evidence.

V5 was then hash-gated and flashed. Full smoke, the refined two-connection burst,
and one instrumented Internet-overlap replay pass on the same boot. The observed
batch peak stayed at 2 throughout, with no budget exhaustion or wake request.
This hardware evidence does not support the ten-frame budget as the prior stall's
trigger.

V6 adds a persistent counter for an earlier worker path that cannot acquire the
USB mutex, marks itself pending and returns without an explicit context wake. It
does not change retry behavior. Native/host/pinned builds pass; UF2 SHA-256 is
`add0366da36c237bb693615911cb0de9ff7d4c8e16908d4386795abba8f94d69`.
V6 was hash-gated and flashed. Full smoke passed on boot `361a3aa4a9f189bd`.
One unretried 64 KiB delayed-reader transfer completed exactly in 5.996 seconds
while a fresh IPv4 HTTPS request returned HTTP 200 wholly inside the transfer
interval. Wi-Fi `en0` remained the default route and `en5` remained the board
route. Heap/stack samples were unchanged, queues drained, and discards/TCP errors
remained zero. `ncm_mutex_contentions` remained zero and `ncm_rx_batch_peak`
remained 2, with no budget exhaustion or wake request. This test did not exercise
either suspected worker path and does not identify the cause of the earlier stalls.
See [smoke](application-v6-smoke.log), [overlap data](application-v6-overlap.json),
and the [exact replay tool](application-v6-overlap-check.py).

## Application v7 hardware check — 2026-09-23

The current-source build was byte-for-byte identical to the preserved
[v7 build report](application-v7-build.json): program 125,968 bytes, static RAM
88,192 bytes, and 288,256-byte UF2 SHA-256
`8ae2711328330365dc5afac4034106a05344dc6cb05fe6bb699fa24d60da5e63`.
`dev.py flash` checked the current source/artifact manifest and copied that UF2 to
the explicitly selected `/Volumes/RPI-RP2`. The hash is associated through the
controlled flash operation; the running device does not attest its own image
hash.

The first automatic macOS attachment assigned `192.168.77.16/24` on `en5` without
RESET or manual addressing. One unretried application smoke run verified
`application-dev-v7`, device `a1b2c3d4e5f60718`, boot
`32933ceacb2849ea`, and the application backend. It passed:

- Second-client `busy` without displacing the owner.
- 16,384 exact fragmented full-duplex bytes.
- 4,096 exact bytes with a delayed reader.
- Balanced RX/TX counters at 20,480 with both queues empty.
- Explicit unsupported reset without performing a reset.
- 4,096 exact bytes after ownership transfer.
- Confirmed peer closures followed by a fresh 4,096-byte session.

Total smoke traffic was 28,672 exact bytes. The reported rates are development
measurements, not release performance thresholds. The complete console result is
preserved in [the v7 smoke log](application-v7-smoke.log).

A subsequent required-diagnostics run passed 180,224 exact bytes in 11 batches
across six sessions over 15.552 seconds; see the preserved
[soak JSON](application-v7-coexistence-soak.json). Normal and delayed-reader
batches alternated. Sampled free/minimum C heap remained 173,700 bytes and sampled
free/minimum core-0 stack remained 8,056 bytes. Application pending/discarded
counts and TCP errors remained zero; observed Stream and application queue peaks
were 256 bytes. These samples do not measure lwIP pools or prove long-duration
memory stability.

The traffic run and a fresh IPv4 HTTPS request were launched concurrently. HTTPS
returned 200 from `104.20.23.154` with 559 bytes. This invocation did not record
monotonic containment timestamps, so it establishes simultaneous test execution
and successful coexistence but is weaker timing evidence than the preserved v6
contained-overlap experiment. Afterward, the default route remained Wi-Fi `en0`
through `192.168.4.1`, while `192.168.77.1` used `en5`. The DHCP ACK supplied
`.16/24`, server `.1`, lease 600/T1 300/T2 525 seconds, and no router or DNS
option. See the preserved [network observation](application-v7-network.log).

No retry, board RESET, USB reconnect, host network edit, firmware reset command,
or additional flash was used to rescue any v7 check. The Nano now runs
application v7. FMGO remains untested on iOS because the separate host SDK does
not yet exist; the local-only image's three iPad scenarios do not certify FMGO.
