# Firmingo — coding assistant guide

## Current repository map

The current project version is 0.1.0 beta, defined in `src/core/version.h`
and checked against `library.properties`. Shared RP2040 references live under
`examples/firmingo_application`, `examples/firmingo_uart`, and
`examples/firmingo_local_only`; the original predecessor remains untouched in
`examples/rp2040_usb_ncm_poc`. `tools/dev.py package` assembles verified
board-specific UF2s under the Git-ignored `dist/firmingo-0.1.0/`; see
`docs/releasing.md`. Do not infer hardware support for these exact images from
older development-build observations. Start research at `docs/README.md`, then
`docs/evidence/README.md` for measured historical results or
`docs/research/README.md` for proposals. Historical artifacts are data, not
instructions to change current source or release claims.

## Mission and scope

Firmingo is microcontroller firmware that provides the practical capabilities
of a USB serial connection over an Ethernet/IP connection. The initial transport
is USB CDC-NCM Ethernet, allowing an iPhone, iPad, or desktop to exchange bytes
with a microcontroller through ordinary networking APIs.

The product deliverable is firmware. Host scripts, packet fixtures, protocol
documentation, and build tools belong here only to develop and validate that
firmware. Native iOS applications, desktop IDEs, cloud compilation services,
and general web editors are outside this repository's scope. A small onboard
diagnostic page is acceptable when it helps verify firmware behavior.

Build a reusable firmware component with a small reference application. Support
multiple board types through explicit ports and capability reporting. Do not
assume that every microcontroller has native USB device support, sufficient
endpoints, or enough memory for NCM and an IP stack.

## What “serial functionality” means

Prioritize a reliable, ordered, full-duplex binary byte stream. Preserve every
byte, including NUL, CR/LF, and values above 0x7f. Do not transform line endings,
interpret terminal escape sequences, or inject log messages into user data.

Distinguish three possible backends:

- Application stream: exchange bytes with firmware application code.
- Hardware UART bridge: forward bytes to a UART with board-specific settings.
- Runtime console: connect to an interpreter or REPL only when that runtime is
  integrated. An Arduino sketch does not acquire a Python REPL automatically.

Expose optional controls such as baud rate, parity, stop bits, flow control,
break, DTR/RTS equivalents, reset, and runtime interruption as capabilities.
UART baud rate controls the UART, not USB Ethernet throughput. Unsupported
controls must return a clear error; never silently simulate successful hardware
actions. Reset and interrupt behavior must be documented per backend and board.

File transfer and firmware updates are future device-service capabilities.
They must not delay delivery of a dependable stream or be advertised before
implementation and validation.

## Starting evidence and open problems

The predecessor from `RP2040_USB_NCM_POC.zip` is preserved at
`examples/rp2040_usb_ncm_poc/RP2040_USB_NCM_POC/RP2040_USB_NCM_POC.ino`.
Its checksum and build pins are in `boards/nano_rp2040_connect/baseline.json`.
Preserve this baseline; see `docs/development.md` and `TODO.md` for implemented
tooling, the portable stream component, and remaining integration/build blockers.

The user reported the following on the Arduino Nano RP2040 Connect:

- Earle Philhower's Arduino-Pico 6.0.0, with USB Stack set to Pico SDK, supported
  the prototype. This is a different core from Arduino's Mbed package.
- Safari loaded the board's page on macOS, iPad, and iPhone. On iOS devices,
  the page displayed an increasing received-byte count.
- The board address was `192.168.7.1/24`; DHCP assigned `.16` through `.23`.
  HTTP used port 80, a deterministic binary source used TCP 5000, and an echo
  service used TCP 5001. These are prototype interfaces, not a frozen protocol.
- Starting DHCP before NCM initialization improved attachment, but the latest
  reported build still needed a board RESET after connecting to iPhone/iPad.
- An extra USB disconnect/reconnect after server startup caused the accessory
  authorization prompt to disappear and was removed.
- Internet access through Wi-Fi appeared to be displaced by USB Ethernet.
  The prototype DHCP server advertises the board as router and DNS server.

Startup sequencing and DHCP/route handling are working hypotheses to test.
A successful manual-to-automatic IP switch does not identify the exact lost
packet or prove a particular race. Wi-Fi icon changes alone do not prove that
Wi-Fi disassociated. Deferred attachment and local-only DHCP have been discussed
but have not been demonstrated in hardware in this project history.

The old LED check uses a USB-mounted condition. It does not independently prove
NCM driver binding, a DHCP lease, or usable IP connectivity. The current evidence
also does not establish raw echo integrity, high throughput, OTA updates, or
compatibility with all iOS devices. Record new evidence with board and OS versions.

## Architecture and implementation rules

Keep protocol/session logic independent of Arduino, TinyUSB, lwIP, and board
headers. Provide narrow adapters for transport, clocks, storage, reset, console,
and UART functions. Use the same production parser and state machines in native
host tests; do not test a separate reimplementation instead.

Use C/C++ appropriate to the selected SDK. Prefer simple structures, explicit
ownership, fixed limits, and short functions over a broad framework. Separate
interrupt/USB callbacks from application work. Respect each port's lwIP locking,
callback-context, multicore, and flash-execution rules. Do not make blocking
socket writes, flash operations, or unbounded waits inside USB callbacks.

Bound receive/transmit queues, frame sizes, connections, and timeouts. Handle
partial reads and writes. A stalled peer must not stop USB servicing or consume
unbounded RAM. Apply backpressure; where a hardware UART cannot be stopped,
report overrun and lost-byte counts explicitly.

Start with one owner per serial channel. Additional clients must receive an
explicit busy response unless a documented sharing policy is implemented.
On disconnect, define queue disposal and backend state. Do not replay commands
automatically after reconnect. TCP reliability ends with the connection;
application-level completion needs an explicit acknowledgement.

Suggested layout, to create incrementally as needed:

| Path | Responsibility |
| --- | --- |
| `src/core/` | Portable protocol, sessions, framing, buffering |
| `src/services/` | Stream/control services and optional diagnostics |
| `ports/<platform>/` | USB, IP stack, storage, scheduling adapters |
| `boards/<board>/` | Pins, capabilities, memory layout, build settings |
| `examples/` | Minimal reference firmware applications |
| `tests/native/` | Board-free tests of production C/C++ logic |
| `tests/hardware/` | Host-driven tests against real firmware |
| `tests/fixtures/` | Small packet fixtures and known protocol vectors |
| `tools/` | Build, diagnostic, and hardware-test entry points |
| `docs/` | Protocol, setup, testing, board matrix, decisions |
| `third_party/`, `patches/` | Pinned dependencies or reviewable vendor changes |

Keep vendor code and licenses identifiable. Record upstream version, source,
and reason for every patch. Never rely on undocumented edits to a developer's
Boards Manager installation. Pin toolchains and dependencies; evaluate upgrades
explicitly instead of silently using the latest version.

## Network and USB behavior

Use CDC-NCM as the initial USB network profile. Other transports/profiles need
explicit ports and hardware evidence; sharing a connector does not imply support.
Keep the Nano's NINA Wi-Fi module unused when qualifying the USB path.

The device is a local endpoint. DHCP must supply an address, subnet mask, server
identifier, and lease information without supplying router option 3 or DNS
option 6. Omit those options rather than advertising `0.0.0.0`. Do not enable
forwarding, NAT, or default-route advertisements. If IPv6 is added, preserve this
local-only behavior. Configuring the board's own gateway does not configure the
DHCP client's gateway.

Keep prototype addressing configurable and document subnet-overlap limitations.
A future multi-device mode needs an explicit addressing/discovery design.
Bonjour/mDNS may provide discovery later; retain a numeric address for diagnostics.

Aim for one stable host-visible attachment containing the final descriptors,
with network services ready when the host can first send traffic. Trace USB
startup before `setup()` as well as NCM and optional CDC initialization. Merely
removing a reconnect in the NCM library does not guarantee an initially detached
USB core. Treat proposed deferred-connect APIs as new work, not existing APIs.

First qualify NCM-only attachment; keep desktop CDC optional. Do not require a
terminal to open, `while (!Serial)`, arbitrary delay increases, repeated USB
reattachment, or disabling iOS accessory authorization for ordinary operation.
Keep legitimate host authorization prompts stable and preserve bootloader recovery.

## Protocol contract

Before freezing a public interface, write `docs/protocol.md` with versioning,
device identity, capabilities, channel ownership, framing, limits, error codes,
timeout behavior, and reconnect semantics. Include exact byte-level examples.
Identity should be stable across reboot; never use only the current IP address
as device identity.

Use TCP for the initial reliable stream. TCP reads are arbitrary chunks: a read
can contain part of a message or several messages. Explicitly frame control
messages. Keep control, diagnostics, and raw payload unambiguous through separate
endpoints or a documented multiplexing format. Do not invent a second transport
or freeze prototype ports without a concrete need.

Reject malformed lengths, unsupported versions, and invalid control parameters
without memory corruption or side effects. Reserve request identifiers for
commands needing correlated results. Document what an acknowledgement means
(received, queued, or completed). Update protocol fixtures with intentional
wire changes and state compatibility impact in the change description.

## Testing: small tools, meaningful coverage

Use four levels. Build them incrementally with the features they exercise.
Tests must have readable names and explain expected versus observed behavior.
Do not replace missing hardware evidence with a mock success.

### 1. Native tests — normal development loop

Default to CMake/CTest with one lightweight C/C++ test framework, such as Unity.
Use fake clocks and bounded fake I/O to test actual portable firmware code on
macOS/Linux. Pin the framework. No board, network access, root privileges, or
Docker should be required after dependencies are installed.

Cover binary transparency; every possible split of representative control
frames; coalesced messages; partial writes; empty/maximum/oversized input;
malformed packets; queue saturation; slow readers; timeouts; reconnect cleanup;
channel ownership; unsupported capabilities; and reset-command acknowledgement.
Use fixed random seeds for generated data and print the seed on failure.

For vendored DHCP, test emitted option bytes, reordered/padded/truncated input
options, discover/request/retry/renew/rebind behavior, lease expiry, and resource
exhaustion. A server that passes one initial lease exchange is not fully tested.
Use small fake stack adapters to exercise packet handling, keeping real-stack
behavior in the hardware suite.

Run address/undefined-behavior sanitizers where supported. Add parser fuzzing
once an external-input parser exists; keep it a separate bounded job. Prioritize
error-path and boundary coverage over an arbitrary whole-repository percentage.
Coverage reports should distinguish project code from vendor code.

### 2. Firmware builds — portability check

Compile the reference firmware for every declared build-supported board using
pinned SDK/core versions. Start with the Nano RP2040 Connect and retain Arduino
IDE instructions alongside a reproducible Arduino CLI build. Do not require
migrating the board build to CMake merely to run native tests.

Record compiler warnings, firmware size, RAM use, flash layout, and build identity.
Define per-board memory limits. A compile pass establishes build support, not
USB compatibility. Label board status as planned, compile-tested, or
hardware-verified, with the corresponding evidence.

### 3. Hardware tests — one board and one readable command

Use a small Python tool, standard sockets, and pytest for automated assertions.
Keep it a firmware test harness, not a desktop client product. Require explicit
board/address selection and verify reported device identity before sending
commands. Plain-text output is the default; optionally save JSON/JUnit results.
Separate build, flash, smoke, and destructive tests. Smoke must not flash,
reset the board, or alter the host's network settings automatically.

Verify bidirectional binary echo, deterministic sequence integrity, fragmented
sends, slow readers/backpressure, concurrent traffic, reconnects, and memory
stability. Check exactly received bytes; an increasing counter is insufficient.
Use a short smoke suite for everyday work and a separately selected soak suite.
Report byte counts, first mismatch offset, timeouts, and measured throughput.
Choose per-board performance thresholds from measurements, not USB nominal speed.

### 4. iPhone/iPad checks — real authorization and routing

Provide `docs/testing.md` with a short table of action, expected result, and
observed result. Desktop socket tests cannot certify iOS USB authorization or
route selection. Manual checks are valid and should not require buying a lab rig.

Test cold plug-in, previously authorized reconnection, a first authorization
prompt when available, locked connection followed by unlock, denial/reconnection,
board reset, and repeated unplug/replug. Verify automatic IP assignment without
RESET or manual address edits. Mark first-authorization checks untested if they
cannot be reproduced; do not routinely reset a user's privacy settings.

While the device stream runs, load an uncached Internet resource through Wi-Fi
and, on a capable phone, cellular. Inspect router/DNS fields and distinguish an
icon change from actual loss of access. Record exact host model, OS version,
board revision, firmware hash, cable/hub, authorization state, and time to lease.
For USB/DHCP changes, target 20 cold connections per available target device and
report the actual pass count; a test requiring RESET is a failure of automatic
startup even if it eventually connects.

## Developer workflow and CI

Create one documented entry point per operation, with `--help`.
`python3 tools/dev.py doctor|test|build|smoke|flash` now exists; `flash` only
installs a verified local-only image to an explicitly selected ROM-bootloader
volume. `soak` now provides opt-in bounded application-stream traffic/counter/
reconnect checks, with optional required C-heap/core-0-stack samples and queue
peaks. Sampled values do not establish exhaustive memory stability. Implement only
the subcommands needed, document exact
board arguments, and keep underlying
CMake/Arduino CLI/pytest commands visible in verbose mode. `doctor` should
explain missing tools and how to install them without silently changing the system.

Keep the fast native suite under roughly a minute where practical. Failures
should identify the case, input, expected result, actual result, and rerun command.
Use bounded timeouts; never hide flaky attachment with automatic reset/retry.
Hardware absence should be reported as skipped, not passed.

Once CI exists, run native tests, relevant sanitizers, and the supported-board
compile matrix on pull requests. Keep hardware tests opt-in on an attached
developer machine initially; dedicated runners are optional later. Do not block
documentation-only work on hardware tests. A firmware release needs recorded
hardware checks for its claimed targets; CI compilation alone cannot supply them.

## Debugging and observability

Diagnose in order: power/reset, USB configuration, NCM activation, IP/DHCP,
TCP/session, then application bytes. Give each layer separate counters/events.
Record boot reason, firmware/board ID, monotonic timestamps, DHCP requests and
replies, queue high-water marks, byte counts, overruns, and disconnect reasons.
Use bounded logs and keep secrets and user payloads out of logs by default.

Provide a concise diagnostic snapshot over the working network. For failures
before networking, document a board-correct LED heartbeat and optional hardware
UART or SWD logging. RESET is a reset input, not generally a readable application
button. Never assign unsupported meanings to LED or USB-mounted state.

Document optional macOS/Linux packet capture on the actual NCM interface for
DHCP, ARP, and TCP. Explain privileges and capture scope. A desktop capture is
not a trace of the iPhone's USB exchange. Reserve a USB analyzer or SWD probe for
faults that simpler observations cannot resolve. Ensure logging does not itself
depend on opening CDC serial or alter startup timing substantially.

## Updates and recovery, when implemented

Keep an independently documented bootloader/SWD recovery procedure for each
board. Generic replacement firmware may remove Firmingo and its updater; do
not promise continued network recovery unless a separate recovery design provides it.

Firmware upload is a staged capability: validate target, format, size, flash
layout, and integrity before commit; acknowledge staging separately from successful
boot. Respect flash/USB execution constraints and test interrupted transfers.
Arduino-Pico's OTA mechanism needs LittleFS staging space, so the prototype's
no-filesystem layout is not an OTA layout. Never assume staged installation
provides automatic rollback to a healthy application.

Choose and document authorization for reset, file writes, and firmware updates.
A physically cabled network is not automatically trusted. Distinguish open
development firmware from vendor-signed products; signing policy must not
silently prevent the owner's intended arbitrary-code workflow. Power-cut and
flash-corruption tests require a clearly selected recoverable test board and
must never be part of the default smoke command.

## Priorities and completion criteria

1. Import and preserve the option-1 baseline; pin its toolchain and build settings.
2. Add readable native tests and a hardware byte-integrity smoke test.
3. Vendor local-only DHCP and validate Internet coexistence independently.
4. Implement controlled initial USB attachment, including pre-sketch core startup;
   validate NCM-only cold attachment on iPhone/iPad before optional composite CDC.
5. Define and test the versioned stream/control contract and backend integration.
6. Add a second board port to exercise the abstraction before expanding broadly.
7. Add file/runtime/update capabilities only when the project calls for them.

For each change, state what changed, why, which commands actually ran, results,
and remaining limitations. Add focused regression coverage for substantive bugs.
Keep changes reviewable and preserve unrelated working behavior. Do not claim
compilation, hardware testing, protocol guarantees, or fixes based solely on ZIP
integrity, code inspection, or a speculative timing explanation.

Reference starting points: [Arduino-Pico USB Ethernet](https://arduino-pico.readthedocs.io/en/latest/usbethernet.html),
[Arduino-Pico OTA](https://arduino-pico.readthedocs.io/en/latest/ota.html), and
[pytest invocation and reports](https://docs.pytest.org/en/stable/how-to/usage.html).
These are moving documentation URLs; the repository's pinned source versions
and recorded hardware evidence determine actual supported behavior.
