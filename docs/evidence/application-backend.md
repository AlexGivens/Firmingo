# Application endpoint implementation — 2026-09-21

Status: **the v2 coexistence stall is packet-captured; v3 reproduced it during an
isolated transfer; v4 and v5 pass their bounded hardware replays**. V5 measured a
maximum of two frames per NCM worker run, so current hardware evidence does not
support the ten-frame budget as the trigger. V6 is compile-tested with a new
USB-mutex-contention counter and awaits flash. Do not yet treat the application
firmware as production coexistence-qualified.

## Resulting behavior

`ApplicationEndpoint` is a portable, allocation-free `StreamBackend` with separate
256-byte host-to-application and application-to-host circular queues. Application
code gets nonblocking partial `read_from_host`/`write_to_host` operations plus
availability, active owner, saturating connection generation and boot-lifetime
queue/discard counters. Full queues return partial/zero counts as backpressure;
ordinary saturation does not discard bytes.

Channel acquisition clears stale bytes, calls `opened(owner)`, and resets session
byte counters. Release, transport disconnect, timeout and backend I/O failure call
`closed()` and `discard()` from application context. Inactive endpoints refuse all
application/network transfers. Pending bytes cannot replay to a later owner; bytes
intentionally removed during terminal cleanup are counted by direction. Busy and
invalid-owner attempts do not notify or alter the endpoint. Raw TCP callbacks still
never call backend or application code.

The hardware-tested image reports `application-dev-v4`; the current source reports
`application-dev-v5`. Its `loop()` explicitly moves
only as many received bytes as the outbound queue can accept, retaining echo as
application behavior for hardware integrity tests. Application logic contains no
TCP, FMGO or USB operations. Existing protocol framing, ownership, controls,
local-only DHCP, NCM startup and NINA-unused behavior remain unchanged.

`device.diagnostics` adds all-or-none application and transport groups. The
transport group contains NCM worker/frame/defer/budget/wake counters and raw-TCP
accept/callback/error/byte counters. Generic backends omit these groups. The host
soak rejects partial or out-of-range fields and checks boot-lifetime counters never
decrease. Worst-case production JSON with UINT32/UINT64 maxima fits the 768-byte
response buffer and is native-tested.

## Verification actually run

- Nine native CTest suites pass with ASan/UBSan: **84 Unity cases**, including
  binary wraparound, exact partial backpressure, inactive behavior, lifecycle,
  disconnect disposal/no replay, application diagnostic schema and worst numeric
  response bounds.
- **114 host pytest cases pass**, including complete/partial/out-of-range and
  lifetime-consistency checks for the added diagnostic group. Localhost sockets
  ran under the previously approved test scope.
- Separate fixed-seed protocol and Session mutation executables pass under
  ASan/UBSan (20,000 cases per executable).
- Pinned Arduino-Pico 6.0.0 / Arduino CLI 1.5.1 application and local-only builds
  pass. Application v4: 120,908 B program, 87,800 B static RAM,
  278,016 B UF2, within 1 MiB/192 KiB project caps. Qualification fallback:
  116,620 B program, 76,816 B static RAM. Six existing upstream
  WiFiClient overloaded-virtual warnings remain; no project warning was emitted.
- Every source/artifact hash in both final reports was rechecked. Preserved baseline
  remains `4887fd480e98025392a85751138127410ad0b314a6ea51c7f18bdd1d1cc3c209`.
  `git diff --check` passes.

Application v2 UF2 SHA-256: `6cf40edf7438d9b8c173556f14ac0fbee2799f71cc4399c0b31b8f48e0f97d57`.
Application v3 UF2 SHA-256: `276704c6bee3606e2ea913896b79cd4b9b1f16ce392d8e6ea29fda6b526e9b24`.
Application v4 UF2 SHA-256: `155521c96f77720ad7abdbfb0d3423a2f41f24b95ebfa87e4a683a48c05e91ba`.
Qualification fallback UF2 SHA-256: `8860b5120570a0d3e7961d5a3f20dec0dcb5b478874a973394fe0ee549a1cfe4`.

Artifacts: [application build](application-backend-application-build.json),
[qualification build](application-backend-local-only-build.json),
[native tests](application-backend-native-final.log),
[host tests](application-backend-host-final.log),
[mutation tests](application-backend-fuzz-final.log),
[application build log](application-backend-firmware-final.log).

V3 artifacts: [build report](application-v3-build.json),
[firmware log](application-v3-firmware.log), [native tests](application-v3-native.log),
and [host tests](application-v3-host.log).

V3 hardware evidence: [smoke](application-v3-smoke.log),
[single-attempt script](application-v3-hardware-check.py),
[result](application-v3-hardware.json), [log](application-v3-hardware.log), and
[post-failure diagnostics](application-v3-post-failure-diagnostics.json).

V4 offline evidence: [build report](application-v4-build.json),
[firmware log](application-v4-firmware.log), [native tests](application-v4-native.log),
and [host tests](application-v4-host.log).

V4 hardware evidence: [smoke](application-v4-smoke.log),
[isolated script](application-v4-isolated-check.py),
[isolated result](application-v4-isolated.json),
[overlap script](application-v4-overlap-check.py),
[overlap result](application-v4-overlap.json), and
[two-minute soak](application-v4-soak-120s.json).

V5 offline evidence: [build report](application-v5-build.json),
[firmware log](application-v5-firmware.log), [native tests](application-v5-native.log),
and [host tests](application-v5-host.log).

V5 hardware evidence: [smoke](application-v5-smoke.log),
[refined burst](application-v5-ncm-burst.json), and
[instrumented Internet overlap](application-v5-overlap.json).

V6 offline evidence: [build report](application-v6-build.json),
[firmware log](application-v6-firmware.log), [native tests](application-v6-native.log),
and [host tests](application-v6-host.log).

## Remaining evidence

The hash-gated v2, v3 and v4 images were installed through explicit RPI-RP2 ROM
recovery. The results below establish explicit-application echo, queue diagnostics,
reconnect cleanup and sampled memory, while retaining the earlier stall and the
limits of one successful v4 replay. A real non-echo application behavior and UART
backend remain later features; UART must declare pins/settings/capabilities and
report unavoidable overruns before it can be advertised.


## v2 hardware results

Target: Nano RP2040 Connect, device `a1b2c3d4e5f60718`, boot
`737993054f7a1daa`, address `192.168.77.1`; MacBookAir10,1, macOS 27.0
(26A428). Board revision, cable/hub and authorization state were not newly
recorded. No physical cold-attachment cycle is counted. Flash verified RPI-RP2,
all current source hashes and UF2 `6cf40edf7438d9b8c173556f14ac0fbee2799f71cc4399c0b31b8f48e0f97d57` before copy/fsync.

Full application smoke passes 28,672 exact bytes: fragmented full-duplex, delayed
reader, BUSY, unsupported reset without reset, ownership transfer, confirmed peer
close and fresh-session reconnect. Hello reports `application-dev-v2`. The endpoint
queues reach 256 bytes in both directions and return to zero. Initial discarded
counts are zero, proving ordinary queue saturation used backpressure.

The required-diagnostics soak ran 120.593405 seconds: **1,310,720
exact bytes**, 80 seeded 16 KiB batches and 20 confirmed-closed TCP
sessions (these are not USB reconnections). Boot ID stayed unchanged; every batch's
backend counters matched and all queues ended empty. C heap stayed 174,732 B,
sampled minimum 174,732 B; approximate core-0 stack stayed 8,120 B,
sampled minimum 8,120 B. Application/Stream peaks stayed 256; application
discard counts stayed zero. lwIP pool and deepest stack remain unmeasured. A fresh
HTTP 200 request also completed while this soak process was active, but was not
traced to one payload interval.

## Large-transfer coexistence regression

All attempts used the same image/boot without board reset, firmware retry or route
mutation. Results are retained independently:

| Attempt | Condition | Result |
| --- | --- | --- |
| 1 | 64 KiB delayed reader plus spawned curl | **Failed** after HTTPS 200; socket timed out around 18 s |
| 2 | Isolated 64 KiB delayed reader | Passed 65,536 exact bytes in 6.002 s |
| 3 | Numbered reproduction with curl | **Failed** after all wire input sent; only 6,656 bytes returned |
| 4 | In-process urllib instead of subprocess | **Failed** after HTTP 200; again 6,656 bytes returned |
| 5 | Isolated 64 KiB with a two-second reader pause | Passed in 7.505 s |
| 6 | Curl plus route query and second diagnostic session | Passed in 5.503 s; instrumentation may have perturbed recovery |
| 7 | Scoped `en5` TCP capture plus unassisted curl overlap | **Failed** after HTTPS 200; 6,656 bytes returned, matching attempts 3 and 4 |

The four unassisted overlap attempts failed; attempt 6 is diagnostic evidence,
not a qualifying retry. HTTP itself succeeded, the board route remained scoped to
`en5`, and post-failure sessions worked with the same boot. After attempt 4,
application rx/tx discard counters were 768 each: three failed terminal sessions disposed
one full 256-byte application queue, matching the lifecycle design and preventing
replay. Heap/stack samples remained unchanged. Ordinary slow-reader backpressure
alone is not sufficient to reproduce the fault. Attempt 7 then located where
network progress stopped.

Attempt 7 captured only `host 192.168.77.1 and tcp port 7420` with a 96-byte
snapshot length. macOS sent all wire input and continued retransmitting the first
unacknowledged segment. The Nano's ACK stopped at sequence `3326902920` while it
still advertised a 7,015-byte receive window. The Nano also retransmitted outbound
segments through sequence `20456` even after macOS had acknowledged `20456`. Thus
the host saw the board's TCP retransmission timer continue while the board stopped
processing inbound data and ACKs. This is evidence at the host NCM interface, not
a USB-bus analyzer trace.

Inspection of the exact pinned Arduino-Pico 6.0.0 source found that the bare-metal
NCM worker processes at most ten receive renewals per run and does not explicitly
wake another run when renewal 10 succeeds. V3 retained the bound and directly set
the worker's pending bit. It flashed and passed complete identity/control/16 KiB/
slow-reader/reconnect smoke, but its first isolated 64 KiB transfer failed before
any Internet request: all 66,176 wire bytes were sent and only 6,656 of 65,536
payload bytes returned before the 18-second timeout. The same boot remained usable;
post-failure application discards increased by 256 bytes in each direction.

That result exposed an error in the v3 fix. `async_context_base_execute_once()`
clears the pending bit before invoking the worker, and directly setting it inside
the callback does not wake the background IRQ. V4 instead releases `USB.mutex`
and calls `async_context_set_work_pending(context, worker)`, which both marks the
worker pending and wakes the async context. Its actual-worker regression verifies
an 11-packet burst as bounded runs of 10 and 1, exactly one SDK wake request, and
that the request occurs after mutex release. Persistent NCM and raw-TCP counters
were added so a later failure can be localized without speculative timing claims.

V4 was then hash-gated and flashed. On Nano device `a1b2c3d4e5f60718`, boot
`37a5706bf6ff3abb`, full smoke passed 28,672 exact bytes including delayed-reader,
ownership, unsupported control, confirmed closure and fresh reconnect checks. Its
single isolated 64 KiB replay passed all 65,536 bytes in 5.997 seconds. The single
planned overlap replay also passed all 65,536 bytes in 5.997 seconds while a fresh
IPv4 Internet request returned HTTP 200; that request began and ended inside the
stream interval. The default route remained Wi-Fi `en0` via `192.168.4.1`, and the
board route remained scoped to `en5`.

A 120.594-second required-diagnostics soak then passed 1,245,184 exact bytes in
76 seeded 16 KiB batches and 19 closed sessions. C heap stayed 174,088 B and the
approximate core-0 stack stayed 8,064 B; application queues ended empty, discards
and raw-TCP errors stayed zero, and the boot ID did not change. NCM worker runs,
received frames and deferred callbacks increased monotonically. Both
`ncm_budget_exhaustions` and `ncm_wake_requests` stayed zero through smoke, both
64 KiB runs and the soak. Therefore the previously failing scenario now passes,
but this hardware evidence neither demonstrates the new continuation branch nor
proves that branch caused the pass.

A bounded two-connection burst diagnostic then explored the branch without reset
or retry. The first 49,408-byte observer burst used a 3 KiB JSON string and hit the
documented 64-byte string limit; its connection closed as a fatal validator-limit
case while the owner still returned 65,536 exact bytes. Replacing the string with
valid whitespace padding still closed the observer before its first response,
consistent with the five-second absolute frame-assembly boundary under this load;
the owner again returned 65,536 exact bytes. Both failed harness shapes are retained.
The refined shape used 64 recoverable 256-byte requests (17,408 wire bytes): all
64 `invalid_argument` responses and the concurrent 65,536-byte echo were exact.
Across that valid run, 440 NCM frames were recorded over 406 worker runs, but the
budget-exhaustion and wake deltas were both zero. This falsifies neither the native
worker fix nor the original fault, but shows the production budget branch was not
reached even by the refined concurrent burst.

DHCP still assigns `.16/24` without router option 3 or DNS option 6. Default route
remains Wi-Fi `en0` via `192.168.4.1`; board route remains `en5`. Internet routing
was preserved during the v4 replay. No failed transfer was silently retried. The
next focused firmware measurement is a persistent maximum-frames-per-worker-run
counter. V5 implements that `ncm_rx_batch_peak` measurement, passes all nine
ASan/UBSan native suites and 114 host cases, and builds at 121,020 B flash and
87,808 B static RAM. Its 278,016-byte UF2 SHA-256 is
`7665d4fc0c23494e8a1954ec95541d64a1ff8d4960a7219d33ba3e3664ae2192`.
At that point V5 was compile-tested only; the following hardware replay supersedes
that status. Broad cold-attachment repetition remains paused.

V5 was subsequently hash-gated and flashed. Full smoke passed on boot
`83c59bf30e43f49e`. The refined burst completed an exact 65,536-byte owner stream
and all 64 recoverable observer responses; a single instrumented Internet-overlap
replay also completed 65,536 exact bytes in 5.997 seconds with HTTP 200 inside the
stream interval. `ncm_rx_batch_peak` was 2 before and after both checks, while
budget exhaustion and wake requests stayed zero. The ten-frame budget is therefore
not supported as the cause of these earlier stalls by current hardware evidence.

Source inspection identified a separate earlier return: when the NCM worker cannot
acquire `USB.mutex`, upstream code increments no diagnostic, sets its own pending
bit, and returns without an explicit context wake. V6 adds only a saturating
`ncm_mutex_contentions` counter for that path. It does not change scheduling.
Nine sanitizer-backed native suites, 114 host cases and the pinned firmware build
pass. V6 uses 121,116 B flash and 87,816 B static RAM; its 278,528-byte UF2 SHA-256
is `add0366da36c237bb693615911cb0de9ff7d4c8e16908d4386795abba8f94d69`.
V6 was then hash-gated and flashed. On boot `361a3aa4a9f189bd`, full smoke passed
28,672 exact bytes across fragmented, delayed-reader, ownership-transfer and fresh-
session reconnect checks. One unretried 64 KiB delayed-reader replay returned all
65,536 bytes in 5.996 seconds while a fresh IPv4 HTTPS request returned HTTP 200
inside the stream interval. The default route remained Wi-Fi `en0`; the board route
remained `en5`. Heap stayed 174,072 B, approximate core-0 stack stayed 8,056 B,
queues drained, and discards/TCP errors stayed zero. `ncm_mutex_contentions` stayed
zero, `ncm_rx_batch_peak` stayed 2, and budget/wake events stayed zero. This replay
did not exercise either suspected NCM worker return path and does not establish the
cause of the earlier v2 stalls. Broad cold-attachment repetition remains paused.

Hardware evidence: [smoke](application-backend-smoke.log),
[soak JSON](application-backend-soak-120s.json),
[network](application-backend-network.log),
[first failure](application-backend-coexistence-attempt1-failed.json),
[isolated pass](application-backend-64k-isolated-attempt2.json),
[reproduced curl failure](application-backend-coexistence-attempt3-failed.json),
[urllib failure](application-backend-coexistence-attempt4-failed.json),
[two-second pause pass](application-backend-slow-reader-2s-attempt5.json),
[instrumented pass](application-backend-coexistence-attempt6-instrumented.json),
[captured failure](application-backend-coexistence-attempt7-captured.json),
[capture metadata](application-backend-coexistence-attempt7-capture.json),
raw packet capture withheld from the public repository (original SHA-256
`00dc09da33cf108b68613e4d4ada7a24168d41ba9aa8a09334879b9fb1170dda`),
[decoded TCP headers](application-backend-coexistence-attempt7-tcpdump.txt),
and [post-attempt-4 diagnostics](application-backend-post-failure-diagnostics.json).

V4 burst evidence: [first fatal-validator shape](application-v4-ncm-burst.json),
[first post-check](application-v4-ncm-burst-post.json),
[recoverable large-frame shape](application-v4-ncm-burst-recoverable.json),
[second post-check](application-v4-ncm-burst-recoverable-post.json),
[refined small-frame result](application-v4-ncm-burst-smallframes.json), and
[preserved tool](application-v4-ncm-burst-tool.py).

V6 evidence: [smoke](application-v6-smoke.log),
[overlap JSON](application-v6-overlap.json),
[overlap log](application-v6-overlap.log), and
[preserved overlap tool](application-v6-overlap-check.py).
