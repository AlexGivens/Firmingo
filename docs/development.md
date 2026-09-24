# Development

Current release version: **0.1.0 beta**. The normal release package includes
board-specific application and UART UF2s; see [releasing](releasing.md).
Older hardware reports in [evidence](evidence/README.md) refer to exact earlier
images and do not qualify newly built 0.1.0 files.

The original sketch remains unchanged under `examples/rp2040_usb_ncm_poc/`.
The local-only qualification fork now integrates `src/core/stream.*` with a bounded
raw-lwIP echo adapter and pinned NCM-only deferred USB startup patch. Native tests
exercise portable code and patched startup functions; hardware qualification is
recorded separately in [echo](evidence/echo.md) and [startup](evidence/usb-startup.md)
reports. Source/HTTP retain prototype I/O limitations; cold attachment and iOS
qualification are pending.

## Setup and test

Use Python 3.10 or later and a working native C/C++ toolchain:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-dev.txt
.venv/bin/python tools/dev.py doctor
.venv/bin/python tools/dev.py test --sanitize
```

CMake 3.31.6 and pytest 8.3.5 are pinned with their Python dependencies. Unity
2.6.1 is vendored with its MIT license and hashes. Once installed, tests need no
Internet, board, root privileges or Docker. Host harness regression tests open
temporary loopback sockets; restricted sandboxes must allow localhost listeners.
The startup tests also require Python and `patch` to apply pinned source patches.
These simulated peers test the harness, not the firmware/network stack.

Commands print their underlying invocation and failures include a rerun command.
`test` runs CMake/CTest for the actual portable core plus pytest harness tests.
The native component uses no heap allocation, platform headers or dynamic queues.
The [FMGO frame/JSON/session components](protocol.md) have separate bounded mutation
jobs. The [application reference](../examples/firmingo_application/README.md) now
integrates a bounded raw-lwIP TCP adapter. Full macOS smoke and a 64 KiB exact
transfer with concurrent Internet access now pass; iOS and sustained/memory checks
remain pending. See [hardware evidence](evidence/application-hardware.md).

```sh
.venv/bin/cmake -S . -B build/protocol-fuzz -DFIRMINGO_SANITIZE=ON -DFIRMINGO_PROTOCOL_FUZZ=ON
.venv/bin/cmake --build build/protocol-fuzz --parallel 2
.venv/bin/ctest --test-dir build/protocol-fuzz -R '(protocol|session)_fuzz' --output-on-failure
```

The jobs run 20,000 deterministic cases each: framing seed `464d4750`, JSON/session
seed `53455331`, printed on failure. Run them separately from the fast native suite.
CI includes them; local passing
results do not imply that the GitHub workflow has run.

On the initial development run, the default `/usr/local` Python/CMake tools could not
run, and the default Xcode selection showed a license prompt. Installed Command
Line Tools worked with an explicit, per-command selection (no system changes):

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
CC=/Library/Developer/CommandLineTools/usr/bin/clang \
CXX=/Library/Developer/CommandLineTools/usr/bin/clang++ \
.venv/bin/python tools/dev.py test --sanitize
```

Use a working ARM-native Python to create the venv if the default Python cannot
run. `CMAKE`, `CTEST`, `CXX`, `CC` and `ARDUINO_CLI` can select explicit tools.
`doctor` reports missing/broken tools; it never installs them. Version probes do
not establish compiler/linker success or that every Arduino helper can execute.

After Xcode/Command Line Tools were reinstalled, a fresh configuration and all
native/host tests passed with the default Xcode selection; the overrides above
are no longer needed on this Mac. Continue using `.venv/bin/python`: default
Python reports 3.9.6 and system CMake still cannot execute. Arduino's ctags
compatibility issue was subsequently resolved by installing Rosetta 2, as recorded below.

## Baseline firmware build

Install Arduino CLI **1.5.1**, then install the pinned core explicitly:

```sh
arduino-cli core update-index --additional-urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
arduino-cli core install rp2040:rp2040@6.0.0 --additional-urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
.venv/bin/python tools/dev.py build --board nano_rp2040_connect
```

The shared local-only, application, and UART references also compile for the
explicit Raspberry Pi Pico port:

```sh
.venv/bin/python tools/dev.py build --board raspberry_pi_pico --firmware local-only
.venv/bin/python tools/dev.py build --board raspberry_pi_pico --firmware application
.venv/bin/python tools/dev.py build --board raspberry_pi_pico --firmware uart
```

The Pico has no preserved predecessor baseline, so `--firmware baseline` is
rejected. Its 2 MiB layout, memory budgets, BOOTSEL recovery, generic RP2040 ROM
volume limitation, local-only hardware result, and remaining compile-only profiles are recorded in
[Pico port evidence](evidence/raspberry-pi-pico.md).

On macOS, the tested IDE contains CLI 1.5.1 at:

```sh
export ARDUINO_CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli'
```

The command checks the original sketch's SHA-256 and installed CLI/core versions
before building. `boards/nano_rp2040_connect/baseline.json` pins all nondefault
board settings; `core-lock.json` records the upstream archive checksum and bundled
tool versions from the installed package index. Arduino verifies downloaded
archive checksums during installation. A version check does not detect manual
edits to installed source; use a clean package installation for release evidence.

Build log, intermediate files, exported images and `report.json` go under
`build/firmware/nano_rp2040_connect/`. Reports record the command, pinned manifest,
outcome and, on success, image hashes, static RAM and flash sizes. The build fails
if its size summary exceeds the initial 1 MiB flash / 192 KiB static RAM caps.
These are engineering budgets, not measured heap/stack guarantees. The 16 MiB
no-filesystem layout reserves 4 KiB for EEPROM and has no OTA staging space.

**Resolved local tool issue (2026-09-16):** Arduino's bundled `ctags`
5.8-arduino11 is an x86_64 executable. Installing Rosetta 2 allowed it to run on
this Mac, and the pinned baseline build completed successfully: 113,716 bytes
reported program storage and 75,048 bytes static RAM, within both budgets.
The build emitted 22 upstream WiFiClient overloaded-virtual warnings. No board
core changes or helper substitutions were required. See the foundation evidence
report for artifact hashes. No upload or hardware test was performed.

## Arduino IDE and recovery

Use Philhower **6.0.0**, board **Arduino Nano RP2040 Connect**, **Pico SDK** USB,
**125 MHz**, **16MB (no FS)**, **IPv4 Only**, **Small (-Os)**, exceptions/RTTI
disabled. The similarly named Arduino Mbed board is a different platform.
Open the preserved `.ino` directly. CDC remains enabled in this baseline;
NCM-only attachment still needs a separately qualified development port.

The predecessor instructions use double-tap RESET to enter its bootloader, then
select the bootloader target in the IDE and upload. That procedure is not newly
hardware-verified here. Keep the manufacturer's Nano RP2040 Connect recovery
instructions available; do not overwrite bootloader areas during early work.
Network firmware replacement/recovery is not implemented. `dev.py` deliberately
has a separate ROM-bootloader-only `flash` command for the local-only image;
there is no automatic reset; application soak is a separately selected command. See [local-only setup](local-only-network.md).

## Portable stream adapter contract

`Stream` uses two 256-byte pending buffers and one nonzero owner token. A second
open returns `busy`, including for the same token. Each poll calls at most one
read and one write per direction. A short/zero write retains the unsent tail;
the source is not read again until that pending buffer is drained. Opposite
directions make progress independently. All bytes are opaque.

Adapters must be nonblocking and single-context, honor buffer bounds, and expose
EOF/failure through `connected()`. A return count larger than the supplied bound
fails closed; the core cannot protect against an adapter actually overwriting
the buffer. No adapter may call these methods from concurrent USB callbacks.

The default idle/no-progress timeout is 30 seconds; constructor value zero
disables it. Expiry is checked before I/O at the exact elapsed boundary.
Read or write progress refreshes the timer. `now_ms` is a wrapping uint32 clock;
poll at least once per clock period. Close, disconnect, timeout and I/O failure
discard both pending buffers and release ownership. The port must close sockets,
clear/reset its own external queues, and apply backend-specific disconnect
policy before assigning another owner. The core never replays pending data.

These are Stream component semantics. `core/protocol.*` implements the separate
FMGO header/length codec described in [protocol.md](protocol.md). `core/json.*` and
`session.*` now validate requests, serialize identity/capabilities, encode busy/errors
and integrate the application backend with Stream. Reset, UART and runtime controls
return unsupported. The TCP adapter must still map connection lifetimes, pbuf
ownership/flow control and socket closure before accepting external clients.
Repeated attachment qualification is paused
at 3/3 by user request; its uncompleted checks remain in the ledger.

`StreamBackend::discard()` must dispose only session queues. Loopback implements
it by clearing its bounded batch. A shared Channel owns the Stream/backend;
Sessions use unique nonzero port-issued owner tokens. Keep backend/Channel alive
until Sessions are destroyed and serialize all polls/closures. Identity is copied
into Session; the board supplies stable device ID and one distinct ID per boot.
The JSON Document borrows its input while tokens/decoded fields are inspected;
Session retains the frame for that lifetime. No production heap allocation occurs.

## CI

`.github/workflows/checks.yml` configures native sanitizer/harness tests and a
pinned Nano baseline compilation on Ubuntu. Build logs/reports are retained as
artifacts. Hardware tests are opt-in only. This workflow is newly added and has
not run on GitHub; its presence is not CI or firmware-build evidence.

## Application-stream reference

```sh
.venv/bin/python tools/dev.py build --board nano_rp2040_connect --firmware application
.venv/bin/python tools/dev.py flash --board nano_rp2040_connect --firmware application --mount /Volumes/RPI-RP2
.venv/bin/python tools/dev.py smoke --board nano_rp2040_connect --firmware application --address 192.168.77.1 --device-id a1b2c3d4e5f60718
```

Use the same pinned CLI/config environment as qualification builds. Select the
physical board's own verified ID, the ROM bootloader volume and a non-overlapping
subnet. `flash` defaults to the older qualification profile; selecting application
is explicit and retains successful-build/source/image hash gates. See
[USB recovery](usb-startup.md). There is no CDC reset path.

Application smoke verifies FMGO hello identity/capabilities before acquiring any
channel. It tests exact full-duplex fragmented echo, a delayed reader, explicit
BUSY with two sessions, status counters, unsupported reset, ownership transfer and
fresh-session reconnect. Unsupported reset checks only rejection; no reset occurs.
A 150 ms reader delay does not by itself establish real-stack queue saturation;
native adapter tests independently verify stalled receive credit and bounded
queues. Smoke performs no Internet or USB attachment qualification. Check routing
and fresh Internet access separately as in [testing](testing.md).

All raw port calls and backend access, including destruction/stop, run under the
Ethernet lwIP lock. Two fixed placement-constructed Sessions share one Channel;
callbacks only retain packets/record faults/acknowledgments. Application poll
performs parsing, backend I/O, queue disposal and bounded closure. Caller-provided
clock is a bounded monotonic millisecond function used at accept. Channel/backend
outlive the server. See [wire/port semantics](protocol.md) and
[recorded integration results](evidence/tcp-session.md).

`ApplicationEndpoint` is the reusable firmware-facing backend. It owns fixed
256-byte inbound/outbound circular queues and exposes nonblocking application
reads/writes without network or protocol types. The Arduino loop invokes it
serially outside the lwIP critical section; raw callbacks never invoke backend or
application code. Channel acquire/terminal cleanup supply explicit lifecycle
notifications. Inactive writes are refused, and close/disconnect discards queued
bytes before another owner can acquire the channel. Queue peaks and discarded
byte counts persist as diagnostics. See the [reference](../examples/firmingo_application/README.md)
and [wire/lifecycle contract](protocol.md#timeouts-disposal-and-adapter-responsibilities).


## Opt-in sustained application traffic

```sh
.venv/bin/python tools/dev.py soak --board nano_rp2040_connect --address 192.168.77.1 --device-id a1b2c3d4e5f60718 --firmware-sha256 5672a839ae6ddd558aca846bd688441decd7c15ccef47230851eebef31fb02a5 --duration 60 --byte-count 16384 --batch-timeout 20 --reconnect-every 4 --output /tmp/firmingo-soak.json
```

Select your own verified board ID, numeric address and declared flashed UF2 hash.
The hash is recorded, not cryptographically verified on-device. This command is
only for the application reference's explicit echo on TCP 7420; qualification/baseline
source/echo are different services. It is absent from default smoke/native/CI.
No USB replug, firmware reset/flash, Internet fetch or host networking change occurs.
The paused physical cold checks stay paused.

Duration is a minimum **run** interval including controls/reconnects, followed by
completion of the current bounded batch/session. Each connect/control/echo is
limited by `--batch-timeout`, and confirmed peer close by at most three seconds;
worst final-iteration overhead is six operation timeouts plus three seconds
(eight when requesting diagnostics).
Limits: run 1..3600 seconds, batch 1..262144 bytes, timeout 1..60 seconds,
reconnect after 1..1000 batches, uint32 seed (default `0x4653474f`). Failures stop
without retry/reset, preserve failed batch/seed and print a rerun command with a
new evidence filename. Interruption is recorded as incomplete, never passed.

Payloads use seeded standard-library PRNG bytes; batch seeds advance by an odd
uint32 increment. The checker compares every returned byte, varies delayed reads,
checks RX/TX/pending queues after each batch, and verifies unchanged boot ID at
each fresh session. It disposes channel ownership and observes peer EOF/RST
before reconnect. Report creation is exclusive; existing JSON is never overwritten.
Progress is checkpointed, with at most 64 detailed samples plus aggregates/last
sample, so long runs do not grow an unbounded in-memory result list.

The recorded first run used firmware without a memory endpoint and marks memory
unavailable. New application builds provide `device.diagnostics`. Add
`--diagnostics` to require a snapshot before channel acquisition in each session
and after every completed batch. Unsupported/invalid measurements fail without
fallback or retry. Reports retain initial/per-batch/last samples; sample counts,
minima and queue peaks must preserve their boot lifetime across reconnects.

These are sampled C heap and approximate core-0 stack measurements; the separate
lwIP memory pool remains unavailable. Minima miss transient allocations and deepest
stack usage. The harness records measurements without declaring leak freedom or
imposing an unmeasured board threshold. Successful bytes/counters alone do not
establish memory stability, hardware UART behavior or iOS compatibility. See
[diagnostic semantics](protocol.md#sampled-runtime-diagnostics-experimental-additive-method)
and the [first traffic-only run](evidence/application-soak.md). The Nano now also
passes a [two-minute required-diagnostics run](evidence/application-diagnostics.md),
with stable sampled values and explicit exclusions; this is development evidence,
not long-duration release qualification.
