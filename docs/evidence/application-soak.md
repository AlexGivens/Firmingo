# First opt-in application traffic soak — 2026-09-18

Status: **traffic/counter/reconnect validation passes; firmware runtime memory
stability remains unmeasured**. This is a bounded 60-second development run, not
long-duration release qualification or iOS evidence. No paused USB cold cycles
were resumed.

## Change and verification

Added `tools/dev.py soak --help`, explicitly selected application reference on
TCP 7420. Host validation uses identity-checked hello, seeded binary payloads,
exact byte comparison, per-batch backend RX/TX/empty-queue assertions, alternating
delayed-reader batches, channel release, observed peer closure and fresh sessions.
Boot ID must stay unchanged across sessions. No firmware reset/flash, USB replug,
Internet fetch, route edit or failure retry is performed by soak.

Bounds: run duration 1..3600 seconds, batches 1..262144 bytes, operation timeouts
1..60 seconds, reconnect after 1..1000 batches. Duration includes control and
reconnect overhead; it is the minimum run interval, followed by finishing the
current bounded batch/session. The worst final iteration adds at most six
operation timeouts plus three seconds of confirmed-close waiting. Result detail
is capped at 64 samples with aggregates/last sample, not an unbounded event list.
Existing output files are rejected before sockets open. Progress/error/interruption
is checkpointed; partial/interrupted runs are not passed. Failure includes batch
seed and a readable rerun command using a new evidence filename.

`Probe.echo(seed=...)` uses bounded seeded standard-library PRNG data while
preserving the existing unseeded smoke pattern. This exercises all byte values
and helps detect stale replay between batches. The firmware wire contract and
firmware source are unchanged; no board flash was needed.

Actual software command:

```sh
.venv/bin/python -m pytest tests/host -q
```

**103 host cases pass**, final run 0.99 s. Nineteen new cases cover seeded real
localhost echo/corruption offset+seed, fake-clock session/aggregate behavior,
changed boot ID, byte/counter/closure failures, interruption, exclusive output,
argument limits and bounded sample storage. Fake I/O cases validate orchestration,
not firmware. No native C/C++ source changed, so the unchanged native suite was
not rerun in this step. Prior sanitizer evidence remains separate.

## Actual Nano run

```sh
.venv/bin/python -u tools/dev.py soak --board nano_rp2040_connect --address 192.168.77.1 --device-id a1b2c3d4e5f60718 --firmware-sha256 5672a839ae6ddd558aca846bd688441decd7c15ccef47230851eebef31fb02a5 --duration 60 --byte-count 16384 --batch-timeout 20 --reconnect-every 4 --output docs/evidence/application-soak-60s.json
```

[JSON result](application-soak-60s.json), [plain output](application-soak-60s.log).
Executed once, exit 0. Host MacBookAir10,1/macOS 27.0 (26A428); Nano RP2040
Connect/device `a1b2c3d4e5f60718`, last verified USB network en5, board `.77.1`,
host `.77.16/24`. Board revision/cable/hub unrecorded. Flashed timer-service UF2
hash is explicitly declared in the command and matches the preceding flash
record; the protocol does not cryptographically attest the on-device image.

| Metric | Observed |
| --- | --- |
| UTC interval | 2026-09-18T22:06:39Z–22:07:39Z |
| Elapsed run | 60.314253 s |
| Exact returned bytes | **606,208** |
| Completed batches | **37 × 16,384 bytes**, varying seeds |
| Successfully closed sessions | **10**; fresh hello between sessions |
| Boot ID | `d362b10ba8d61658`, unchanged |
| Individual echo intervals | 1.001397–1.989836 s |
| Counter checks | Each batch RX=TX=cumulative session bytes; pending queues zero |
| Runtime heap/stack observation | **Unavailable**, explicitly recorded |

The run proves byte integrity/counter consistency and cleanup across these tested
sessions. It does not measure TCP pool high-water marks, heap leaks, stack use,
maximum throughput, hardware UART overruns or Internet access during this run.
The preceding [64 KiB concurrent-Internet test](application-hardware.md) remains
separate evidence. iPhone/iPad and longer traffic/memory tests stay open.

## Build metadata and next work

Because the firmware build report also hashes `tools/dev.py`, its new command
requires refreshing application/qualification build metadata for future verified
flashes. Cached pinned builds run through the usual CLI entry points; the
application UF2 must reproduce the already-tested timer-service hash before
counting it as the same firmware. No additional board flash is required for
identical output. See [refreshed build metadata](application-soak-build.json).

Next: firmware runtime heap/stack/queue diagnostics for meaningful memory
observations, or real application/UART backend integration with explicit
capabilities. Do not mark the broader memory/soak acceptance item complete based
on this short traffic-only run.

Cached builds passed: application program/static RAM 117,036/86,560 bytes;
qualification 116,604/76,808 bytes. Application UF2 reproduced the tested hash
exactly; qualification reproduced `dd6430cc4e15ff7d44f1cc2e553b11d20e4102855628963816c7470bb9cb2e5b`.
Six upstream WiFiClient warnings per final build, no project warnings. No reflash
or native rerun occurred. Offline reconstruction of the 37 recorded seeds confirms
every tested 16 KiB payload contained all 256 byte values.
