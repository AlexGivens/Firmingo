# Firmingo — project work ledger

Read [AGENTS.md](AGENTS.md) for scope and engineering rules. This file records
changing priorities, acceptance checks, and evidence; it is not a second policy file.

## How to maintain this file

- Keep tasks unchecked until their stated acceptance checks have evidence. Use
  `Status: in progress` on at most one milestone; split independent work into
  smaller tasks when needed. Use `Status: blocked` with a reason and next action.
- For completed work, record the commit/artifact, exact commands and results, and
  a result/report link when available. Manual evidence must identify the board,
  firmware, host/OS, and observed outcome. Label user reports explicitly.
- Compilation, native tests, hardware tests, and iOS qualification are separate
  claims. Record skips and missing evidence; do not convert a plan into a pass.
- Update relevant items after meaningful work and before handoff. Keep unresolved
  failures visible. Record material decisions below; move lengthy reports into
  `docs/` and link them here. Do not invent commit IDs, dates, or test results.
- A task list is not authorization to expand the user's requested scope. Optional
  backlog items stay deferred until selected.

## Current position and next action

Version 0.1.0 beta has board-specific build and package metadata. The shared
application and UART sketches compile for Nano RP2040 Connect and Raspberry Pi
Pico; exact 0.1.0 images are not yet hardware-qualified. Prior Nano and Pico
results and failed experiments are preserved in [the evidence index](docs/evidence/README.md).
The protocol remains a development contract. Release profiles have no upload
target; the separate experimental M9 path implements constrained Nano managed
sketch replacement with the measured scope below.

The local 0.1.0 package contains both application and UART targets for Nano and
Pico. On 2026-09-24, all four packaged UF2s, build reports, current source
hashes, manifest, and ZIP contents were verified together. The sanitizer-backed
native suite passed 10/10 and the host suite passed 163/163 with loopback socket
access. No rebuild or hardware check ran in that session: the installed
Arduino-Pico core is 6.1.0, while the build pin is 6.0.0. Next: perform
explicit-target smoke and iOS checks for the exact 0.1.0 hashes before
publishing a hardware-verified release. Pico UART, repeated cold attachment,
long-duration traffic, and iOS FMGO remain open. Do not infer these results from
earlier `application-dev-v7` or `uart-dev-v2` images.

The next product goal is a beginner workflow on **one Nano RP2040 Connect and one
USB cable**: edit a supported Arduino sketch in a separate IDE, upload it over
Firmingo's USB Ethernet connection, and exchange sketch console bytes in that IDE.
M7–M10 below specify the firmware and cross-project acceptance work. They are
separate from the exact-image 0.1.0 qualification work above. The constrained
M7 proof is implemented and hardware-tested on one Nano; M8–M10 remain open.
M9 now has successive network installs, exact console, status reconciliation,
and one pre-commit power-loss observation. The firmware-side M10 SDK handoff
is prepared; actual SDK/IDE and iPad workflow acceptance remain open.
This proof does not make the 0.1.0 UF2s managed-sketch images or qualify a
general Arduino runtime. Pico support follows only after its smaller flash
layout is assessed. See the [M7 feasibility note](docs/research/managed-sketch-feasibility.md)
and [Nano hardware record](docs/evidence/managed-sketch-m7exp3.md).

## Starting evidence — predecessor prototype

Source artifact: `RP2040_USB_NCM_POC.zip`, sketch `RP2040_USB_NCM_POC.ino`.
These observations describe the predecessor, not verified Firmingo releases.

| Area | Evidence and limits |
| --- | --- |
| Board/core | Arduino Nano RP2040 Connect; user used Philhower Arduino-Pico 6.0.0, USB Stack Pico SDK, rather than Arduino Mbed. Reproduce the build after import. |
| HTTP/stream | User reported Safari success on macOS, iPad, and iPhone, with an increasing received-byte counter on iOS. Exact echo integrity and performance remain unverified. |
| Addresses/ports | Board `192.168.7.1/24`; prototype DHCP pool `.16`–`.23`; HTTP 80, deterministic TCP source 5000, echo 5001. Inspect imported source to confirm; these are not frozen protocol choices. |
| Startup | Starting DHCP before NCM initialization (option 1) improved behavior. Latest user report still required one board RESET after iPhone/iPad connection. |
| Re-enumeration | Extra USB disconnect/reconnect after service startup disrupted the accessory prompt and was reverted. |
| Routing | User reported apparent Wi-Fi displacement. Prototype DHCP advertises the board as router and DNS server; local-only DHCP has not been hardware-qualified. |
| LED | Old LED logic indicates USB-mounted state, not independently NCM binding, DHCP, or IP success. |

The exact startup race remains a hypothesis. Success after manual-to-automatic
address changes does not identify a lost packet. Wi-Fi icon changes alone do not
prove disassociation. Deferred initial attachment has been discussed, not shown
to work. There is no evidence here of OTA, universal iOS compatibility, or a
completed assistant-run firmware compilation in the predecessor history; the new
2026-09-16 compile result is recorded separately below.

## Ordered milestones

### M1 — Preserve and reproduce the baseline

Status: compile-tested; hardware reproduction pending. Rosetta 2 resolved the local ctags blocker.

- [x] Import the reference sketch into `examples/` with Nano board settings;
  preserve an identifiable baseline before changing behavior.
- [x] Pin Arduino-Pico/tool versions and record USB, flash, and board selections.
  Keep patches/dependencies reproducible without hidden Boards Manager edits.
- [x] Document Arduino IDE and CLI setup/build/recovery in `docs/`; add an initial
  board matrix distinguishing reported, compile-tested, and verified evidence.
- [x] Build the imported firmware; record command, warnings, hash, size, RAM/flash
  layout, and any build blocker. Define initial per-board memory budgets.
- [ ] Reproduce current behavior on available hardware and record limitations,
  including whether RESET remains necessary.

Acceptance: another developer can build the same source with documented settings;
recorded baseline observations distinguish build success from hardware behavior.
Evidence: [foundation report](docs/evidence/foundation.md); baseline compilation
passed on 2026-09-16 with recorded sizes, warnings and artifact hashes. Source SHA-256 and settings are in the board manifest.

### M2 — Establish readable tests and diagnostics

Status: partially implemented; smoke/startup, sampled runtime memory and opt-in
measured soak present. Long-duration and exhaustive memory evidence remain open.

- [x] Establish native CMake/CTest with one pinned lightweight framework. Test
  production portable code, fake clocks, and bounded I/O; add a small real behavior
  test before expanding the harness.
- [x] Add a Python sockets/pytest smoke harness with explicit address selection,
  target confirmation/identity where available, exact echo/sequence comparison,
  bounded timeouts, mismatch offsets, byte counts, and measured throughput.
  Identify any temporary lack of protocol-level identity in the old prototype.
- [ ] Exercise fragmented sends, slow readers, concurrent traffic, disconnect/
  reconnect, and memory stability; keep long soak testing separately selected.
- [x] Provide only needed `tools/dev.py` subcommands with help and visible underlying
  commands. Proposed interface: `doctor`, `test`, `build`, `flash`, `smoke`, `soak`.
  Implemented: doctor/test/build/flash/smoke/attach/soak. Soak is opt-in
  application traffic/counter/reconnect validation with optional required memory samples.
- [ ] Add supported sanitizers and PR CI for native tests and the initial firmware
  build. Distinguish absent hardware from successful hardware tests.
- [x] Add sampled C heap/core-0 stack and persistent Stream queue diagnostics,
  validated in a two-minute Nano soak; lwIP pool/exhaustive stack remain unavailable.
  See [diagnostics evidence](docs/evidence/application-diagnostics.md).
- [ ] Add bounded startup events and complete the diagnostic snapshot; document LED meanings,
  optional UART/SWD access, and scoped desktop DHCP/ARP/TCP capture.
- [x] Create `docs/testing.md` with the iOS checklist below and a simple report
  template; keep smoke nondestructive and test output readable without CI.

Acceptance: a developer can run a short native suite without a board and a
separate explicit-target byte-integrity smoke test with one board; failures show
expected/actual data and a rerun command. Record which cases actually ran.
Acceptance evidence: all three macOS smoke cases pass after the bounded echo fix;
see [echo evidence](docs/evidence/echo.md). Long soak and memory stability remain open.

Initial evidence: eight native Unity cases pass with ASan/UBSan; 22 host harness
tests pass. Hardware suite: three skipped, no target selected. CI definition
includes native sanitizers and a pinned Nano baseline build, but has not run on
GitHub. Long-duration memory stability and bounded startup events remain open;
board integration now covers the qualification echo endpoint. See [foundation report](docs/evidence/foundation.md).

### M3 — Preserve Internet access alongside the board

Status: qualification paused by user. Offline tests/build and macOS coexistence checks pass; repeated attachment and iOS/cellular qualification pending.

- [x] Vendor/patch the DHCP server with upstream version, license, and rationale.
  Omit router option 3 and DNS option 6 while retaining correct lease information.
- [x] Test exact emitted options and reordered, padded, or truncated input; cover
  discover/request, retries, renewal, rebinding, expiry, and pool exhaustion.
  Keep fake-stack logic tests distinct from real-stack qualification.
- [ ] Verify automatic addressing on macOS and available iPhone/iPad targets.
  Record assigned address, mask, router/DNS fields, and actual pass counts.
- [ ] While streaming board bytes, verify an uncached Internet request through
  Wi-Fi and, where available, cellular. Record subnet conflicts or route failures.

Acceptance: observed DHCP replies omit router/DNS options; tested hosts retain
board connectivity and Internet access. Assess this independently of the USB
startup fix and distinguish route problems from Wi-Fi disassociation.
Evidence: pending.

### M4 — Make cold attachment work without RESET

Status: implementation and three macOS cold functional checks complete, two timed; remaining repeated cold and iOS qualification pending.

- [ ] Trace pre-`setup()` USB initialization, descriptor registration, NCM activation,
  DHCP readiness, and optional CDC startup. Identify the actual readiness gap
  before choosing an API or claiming a specific race.
- [x] Implement deferred initial attachment across the necessary core/library
  paths so final descriptors and services are ready before host-visible traffic.
  Keep changes as reviewable pinned patches; preserve normal bootloader recovery.
- [ ] Qualify NCM-only first, then optional composite CDC. Confirm normal startup
  never depends on serial-monitor attachment or forced re-enumeration.
- [ ] Add meaningful sequencing regressions where testable and run the iOS
  attachment checklist. Record all failures, including transient approval prompts.

Acceptance: stable authorization and automatic DHCP/page/stream access on tested
iPhone/iPad targets without manual IP changes or RESET; target 20 cold attachments
per available target and report actual successes/attempts. Model tests alone do
not complete this milestone.
Evidence: [startup report](docs/evidence/usb-startup.md): 28 native Unity cases
with sanitizers, 39 host tests, pinned compile and flashed NCM-only image. First
post-flash readiness request failed; subsequent identity-verified smoke and
IPv4/IPv6 Internet coexistence pass. Physical macOS reconnections: 3 functional
passes / 3 attempts toward 20; cycles 2–3 readiness timing recorded. The recorded
iPad has 3 functional physical connections / 3 attempts toward 20: an unlocked
first-authorization connection, a previously authorized reconnect, and a
locked-then-unlocked connection. All reached exact browser records without RESET
or manual IP configuration; the first also passed Wi-Fi Internet coexistence.
The host displayed a legitimate Allow prompt on the first two connections; exact
prompt behavior was not reported for the locked attempt. See the
[cold-cycle ledger](docs/evidence/macos-cold.md) and
[iPad qualification evidence](docs/evidence/ios-qualification.md).
Source-derived call ordering does not establish the exact historical USB packet
race.

### M5 — Define and implement the reusable serial service

Status: complete for the initial Nano application/UART contract. Portable FMGO
sessions and bounded Nano TCP integration are implemented. The reusable
application endpoint passes smoke and measured traffic;
the v2 large-transfer Internet-overlap failure is preserved, while the same bounded
scenario passes once each on v4, v5 and v6 without exercising either proposed NCM
worker continuation path.

- [x] Implement the README's bounded FMGO frame codec, exact shared wire fixtures,
  split/coalesced/header-limit tests and a separate bounded mutation job.
  Eight new native cases, all six sanitizer suites (36 Unity cases), 60 host tests
  and 20,000 parser mutation cases pass. Pinned Nano qualification build passes;
  no new listener or board flash. See [evidence](docs/evidence/protocol-framing.md).

- [x] Write `docs/protocol.md` with version/identity/capabilities, ownership, framing,
  limits, errors, request IDs, acknowledgement stages, and reconnect semantics.
  Experimental application contract and request/result schemas implemented and
  native-tested; no public freeze or hardware interoperability claim.
- [x] Implement bounded strict JSON validation and application Session/Channel,
  explicit busy/unsupported controls, timers, status counters and queue disposal.
  21 new cases; all seven sanitizer suites (57 Unity cases), 60 host tests and
  separate 20,000-case JSON/session mutation job pass. Pinned Nano library build
  passes; current sketch does not expose the new service. See
  [evidence](docs/evidence/session.md).
- [x] Integrate two fixed raw-lwIP TCP Session slots with the minimal Nano
  application reference. Preserve qualification/baseline endpoints; no callback
  parses requests or runs backend work. Add explicit application build/flash/smoke
  selection. Ten adapter cases and three reference startup/identity cases pass;
  pinned application and qualification builds pass. See
  [evidence](docs/evidence/tcp-session.md).
- [x] Flash application reference; verify exact framed bytes, BUSY/transfer,
  unsupported controls and reconnect on real TCP/lwIP, while retaining Internet.
  Current application v7 passes one full macOS smoke (28,672 exact bytes) plus
  a 180,224-byte bounded diagnostic run across six sessions while a fresh HTTPS
  request succeeds. The earlier v6 contained-overlap replay remains stronger
  concurrency-timing evidence. No reset/retry rescues failures. FMGO on iOS and
  longer v7 soak work remain separate open checks. See
  [hardware evidence](docs/evidence/application-hardware.md).
- [x] Implement the portable allocation-free application endpoint with narrow
  board/transport boundaries, two bounded queues, partial backpressure, explicit
  open/terminal lifecycle, no replay and queue/discard diagnostics. The reference
  sketch now performs echo in application code. See
  [application-backend evidence](docs/evidence/application-backend.md).
- [x] Implement an explicitly capability-described UART backend separately.
  The allocation-free backend and Nano `Serial1` port advertise supported UART
  settings, report actual baud, reject unsupported controls, gate ownership on
  hardware TX drain, and expose receive overrun/loss diagnostics. UART v1 exposed
  a sustained-loopback RX overrun; UART v2 adds bounded local-TX backpressure and
  a throttle counter. V2 and application v7 compile within board budgets.
  See [UART evidence](docs/evidence/uart.md).
- [x] Add exact wire vectors and tests for binary transparency, every split of
  representative control frames, coalesced messages, partial writes, empty/max/
  oversized input, malformed packets, queue saturation, slow readers, timeouts,
  ownership, unsupported controls, reset acknowledgement, and reconnect cleanup.
  A coverage audit maps each requirement to named production-code tests; all 10
  sanitizer-enabled native executables and all 153 host tests pass. See
  [coverage evidence](docs/evidence/protocol-coverage.md).
- [x] Verify UART settings/overruns on applicable hardware. Keep runtime-console
  controls unsupported until a runtime is integrated. D1-to-D0 exact loopback at
  requested 115200 and 57600 passes. V1's first soak proved RX overrun/loss; V2's
  unretried replay passes 458,752 exact bytes across seven sessions with exercised
  throttling and zero overrun/loss. An independent Pico peer passes exact traffic
  in each direction at 115200 and 57600. A controlled unsolicited burst verifies
  bounded FIFO saturation, conservative minimum-loss accounting, acquisition
  drain, and exact recovery. The v2 Pico fixture also passes all 24 advertised
  data/parity/stop-bit combinations at 57600, bounded exact traffic at 300 and
  2,000,000 baud, and seven invalid-setting rejections on each device. A separate
  1,024-byte uninterrupted burst at 2 Mbaud records three Nano RX overruns; this
  is the expected no-CTS burst limit, not a passing reliability claim.
- [x] Add separate bounded frame and JSON/session mutation jobs once external-input
  parsers exist; pair with focused boundary/error native tests. Each job passes
  20,000 cases with a printed fixed seed. Coverage is component behavior evidence,
  not an asserted repository-wide percentage. See [session evidence](docs/evidence/session.md).

Acceptance: documented protocol matches production firmware and fixtures; exact
bidirectional bytes, control results, bounded resource use, and reconnect behavior
are verified with the applicable native and hardware suites.
Evidence: [protocol contract](docs/protocol.md),
[coverage audit](docs/evidence/protocol-coverage.md),
[application hardware](docs/evidence/application-hardware.md), and
[UART hardware](docs/evidence/uart.md). The interface remains experimental and
unfrozen; authentication, upload, and runtime services are not advertised.

### M6 — Validate portability with a second board

Status: complete for the Pico application port; UART remains compile-tested only.

- [x] Select the Raspberry Pi Pico after checking native USB device support,
  resources, BOOTSEL recovery, and Arduino-Pico network support. It exercises a
  second board identity, 2 MiB flash layout and no-Wi-Fi capability while sharing
  the RP2040/Arduino-Pico adapter; this does not count as a second silicon/SDK port.
- [x] Add its manifest, explicit compile-time identity, capabilities, pinned
  application/local-only/UART builds, and memory budgets without leaking board
  headers into the portable core. All three profiles compile within budget.
- [x] Extend build CI and run the applicable stream, startup, routing, and recovery
  checks; record support status and remaining differences. The CI matrix is
  extended, and one Pico local-only macOS session passes BOOTSEL flash/recovery,
  automatic NCM/DHCP, exact source/echo/reconnect, and concurrent IPv4/IPv6
  Internet access. Application v7 passes complete FMGO smoke, a 180,224-byte
  diagnostics-required run across six sessions, and a contained 65,536-byte
  exact transfer with concurrent IPv4/IPv6 Internet access. A subsequent
  one-minute diagnostics run passes 622,592 exact bytes across 10 sessions.
  UART, repeated attachment, iOS, long-duration traffic, and the unrun GitHub
  workflow remain open.

Acceptance: shared protocol/core works on two explicitly qualified board ports;
compile-only support is labeled separately from hardware verification.
Evidence: [Raspberry Pi Pico port](docs/evidence/raspberry-pi-pico.md).

### M7 — Prove a persistent, single-board sketch architecture

Status: single-Nano M7 proof complete; broader qualification remains open.
The selected Nano demonstrates one persistent
resident and an exact A-to-B network sketch replacement with D2 physically
armed. Blink B passed exact-byte smoke before and after a normal five-second
unplug/replug; its observed D13 half-period was about 500 ms. An unarmed
header-only request returned `D`. The composite UF2 copy had updated the
resident but left an old sketch slot, so an authorized direct slot write and
readback were needed before the network proof. The cause of that ROM copy
behavior remains open; a later exact Blink B `picotool` load and full UF2
verification succeeded, followed by new-boot exact-byte smoke. A deliberately
nonreturning core-1 sketch subsequently left the resident reachable and
timed out replacement without changing the slot; exact ROM readback and
Blink B recovery passed. Broader sketch compatibility, other stall classes,
repeated attachments, and iOS behavior remain open. See
[m7exp2](docs/evidence/managed-sketch-m7exp2.md) and
[m7exp3](docs/evidence/managed-sketch-m7exp3.md) evidence.

- [x] Qualify the new full-composite `picotool` initial-install path on a
  recoverable Nano. Its host checks require the ROM flash ID and verify all
  UF2 blocks before reboot. On the selected Nano, the exact Blink B composite
  (SHA-256 `66b3cc527ffc175dc086e25adf6ed99c1bbae2d4be7036cb110701075294eb05`)
  loaded, both UF2 ranges verified, new-boot exact-byte smoke passed, and the
  user observed roughly 500 ms D13 on/off intervals.
  Keep the mass-storage copy path labeled unverified until its skipped slot
  blocks are explained.
- [x] Decide and document a Nano-specific resident-service, boot and application
  layout. Account for the pinned Arduino-Pico core, 16 MiB flash, image staging,
  metadata, EEPROM/filesystem use, RAM/stack budgets, USB ownership and flash
  execution rules. Preserve ROM recovery and keep the NINA module unused. Record
  whether a pinned Arduino-Pico OTA mechanism can satisfy the design or needs a
  reviewable board/core port; do not assume its LittleFS layout fits today's
  no-filesystem images. The [Nano layout decision](docs/research/managed-sketch-nano-layout.md)
  reserves stage and metadata ranges but limits this proof to a 4 KiB RAM-held
  capsule and one-sector slot write. Larger image support and power-cut
  recovery require implementation and hardware qualification.
- [x] Define the experimental sketch contract: which Arduino APIs and libraries run,
  how `setup()`/`loop()` are scheduled, how `Serial` maps to the Firmingo console,
  what happens when no console is open, and which USB/flash operations conflict
  with the resident service. A conventional whole-board UF2 that replaces
  Firmingo is not a managed sketch image. The current proof explicitly claims
  only the tested D13/clock/delay/byte-console facade; see
  [Nano proof contract](docs/research/managed-sketch-contract.md). General
  GPIO error semantics and broader Arduino compatibility remain open.
- [x] Build a minimal proof image and two different Blink/console sketches. Use
  the existing ROM path for initial installation only; prove the second sketch
  can replace the first while Firmingo remains available. The pinned resident
  build reports 131,752 flash bytes and 93,472 static RAM bytes; the proof
  stages at most 4 KiB, and the two modules have 432/436 code bytes. The
  selected Nano ran the A-to-B replacement and exact-byte stream checks.
  Pico's separate 2 MiB layout still needs assessment.
- [x] Test normal `delay()`/long loop work, sketch reset, cold reconnect, and a
  deliberately stalled sketch. State which failures the resident service can
  survive and which require physical ROM recovery. If ordinary sketch behavior
  cannot leave the network/console usable, narrow the compatibility claim or
  choose a separate persistent bridge/programmer before implementing upload.
  A bounded five-second `delay()` diagnostic module returned the expected
  one-second park timeout on the Nano through the resident's upload listener.
  The old diagnostic sketch resumed and echoed over FMGO. Blink B was restored
  with full exact-byte smoke; after D2 removal, the user confirmed D13 still
  blinking appropriately and FMGO reported the same boot. A deliberately
  nonreturning core-1 module also ran on the Nano:
  the resident answered FMGO hello and refused a replacement with `T` after
  1.027 s on the same boot. ROM readback matched all 4,096 stalled-slot bytes,
  and the exact Blink B composite passed full verification and new-boot smoke
  after recovery; the user observed Blink B's roughly 500 ms D13 half-period.
  Sketch-only reset is unsupported: `device.reset` returned `unsupported`
  and a fresh FMGO hello kept the same boot ID. One normal unplug/replug
  retained Blink B and passed full smoke. Physical RESET, power-cut recovery,
  broader cold reconnect behavior, and stalls affecting interrupts/shared RAM
  remain untested; these are outside this constrained proof claim.

Acceptance: a board-specific design and measured prototype show two successive
managed sketches can run without erasing Firmingo, with an explicit compatibility
boundary and recovery procedure. Evidence: design decision, memory map, exact
build hashes/sizes and Nano observations; [feasibility note](docs/research/managed-sketch-feasibility.md)
and [experimental build](experiments/managed_sketch/README.md) plus the
[first Nano observation](docs/evidence/managed-sketch-initial.md) exist.

### M8 — Make the sketch console behave like an IDE serial monitor

Status: `m8exp1` and `m8exp3` booted but their first Nano echoes received zero
bytes. `m8exp2` showed a stalled loop counter despite a later D13 blink
observation. The exact `m8exp4` image passed Nano console smoke before and after
one cold replug, including exact echo, slow reader, ownership transfer, and
reconnect. A concurrent macOS Wi-Fi Internet check and one five-minute host
sleep/wake cycle passed; the latter preserved the boot ID. An overnight
solid-on D13 observation on `m8exp3` remains unexplained. Overnight behavior,
an open connection across sleep, and actual IDE-client acceptance remain open.
The constrained facade still has no mutable globals or general Arduino library
support.

- [x] Add a narrow Arduino-facing console adapter to the managed-sketch build so
  `Serial.print`/`Serial.write` output and `Serial.read` input use the existing
  application stream. Keep user bytes separate from diagnostics and protocol
  control; do not translate line endings or claim a UART baud rate.
- [x] Define console-open/close, pre-open output policy, queue limits, backpressure,
  dropped-byte counters and reboot/reconnect behavior. One owner remains the
  default. Do not let `while (!Serial)` or a disconnected IDE indefinitely block
  normal sketch startup.
- [x] Native-test the production adapter with NUL, CR/LF and all byte values;
  fragmented/coalesced traffic, partial writes, slow readers, saturation, busy
  second client, disconnect and fresh ownership. On Nano hardware, run an echo
  sketch and compare exact bidirectional bytes and counters across a cold replug
  while Wi-Fi Internet remains usable.
  The native suite passed, and the exact `m8exp4` Blink B image passed full
  console smoke before and after one normal Nano replug. A simultaneous
  128 KiB exact echo and uncached HTTPS request through macOS Wi-Fi passed;
  see the [M8 hardware record](docs/evidence/managed-sketch-m8exp4.md).

Acceptance: a supported sketch can print and read exact bytes through the FMGO
console, and the IDE can reconnect without resetting or reflashing the board.
Evidence: shared protocol fixtures, native tests and exact-image Nano smoke;
actual IDE-client reconnect, an open connection across sleep, and overnight
behavior are still untested.

### M9 — Install managed sketches over USB Ethernet

Status: the `m9exp1` Nano candidate implements a physically armed
4 KiB FMGO upload target, retained transaction status, and managed-generation
confirmation. Native, sanitized mutation, host-harness, and pinned builds pass.
Initial ROM readback, exact console smoke, and unarmed-upload refusal passed.
Successive network installs (Blink A then B), console smoke after each, deliberate
pre-commit disconnect, rejection checks, and status reconciliation after a
discarded commit result passed. Blink A's 250 ms and Blink B's 500 ms LED
half-periods were observed. One power cut after RAM verification and before
commit passed: installed Blink B survived, RAM status was lost, console smoke
passed, and a fresh network install succeeded. Power loss during flash writing
remains untested. M9 stays on
the Nano until the user explicitly requests exact `m8exp4` restoration.
Release references still advertise `targets:[]`.

- [x] Specify one `target_id` and one accepted image format for the Nano managed
  sketch slot. Update `docs/protocol.md` and shared wire fixtures for `flash.begin`,
  chunk, `flash.status`, `flash.finish`, `flash.commit` and `flash.abort`; define
  explicit state, ownership, authentication/authorization, limits, errors and
  acknowledgements. Do not advertise a target until its backend works.
- [x] Implement bounded staging and install outside USB/lwIP callbacks. Validate
  exact board/target, format, address range, size and SHA-256 before commit;
  protect resident code/metadata, keep flash layout and build manifest aligned,
  and report accepted, verified, installed and boot-confirmed as distinct states.
  SHA-256 checks integrity, not authorization or provenance.
- [x] Define interruption semantics for disconnect, reboot and power loss.
  A lost commit response must lead the host to query status, not replay commit.
  Preserve ROM recovery; do not claim rollback until it is implemented and tested.
- [x] Native-test wrong-board images, malformed/duplicate/out-of-order chunks,
  offset overflow, short/oversize images, hash mismatch, insufficient storage,
  unauthorized requests, concurrent upload/console activity and every install
  state transition against the production parser and staging interface. Keep
  parser fuzzing bounded and separate.
- [ ] Add an **opt-in** explicit-target hardware upload harness, separate from
  smoke. On a recoverable Nano, upload two different sketches consecutively
  without BOOTSEL/REC or manual network changes; confirm each boot's identity and
  behavior. Repeat with deliberate disconnect before commit and power cuts at
  selected stages, recording recovery and uncertain outcomes. Never run these
  destructive cases in default smoke or CI hardware checks.
  The opt-in `fmgo_upload.py` harness supports exact-build install, deliberate
  pre-commit disconnect, and read-only status reconciliation. A then B installs,
  exact console checks, pre-commit disposal, and deliberately discarded commit
  result reconciliation and both LED half-period observations passed. One
  verified-RAM-stage power-loss case passed, followed by exact console smoke
  and a fresh upload. Power loss during erase/program remains untested; the
  combined interruption-qualification item stays open.

Acceptance: an authorized, board-matched managed sketch installs over the cabled
NCM link, boots, and permits another network upload; rejected or interrupted
uploads cannot be reported as successful. Evidence: protocol vectors, native
results and exact UF2/image hashes are in the [M9 build record](docs/evidence/managed-sketch-m9exp1.md);
the [M9 hardware record](docs/evidence/managed-sketch-m9exp1-hardware.md) tracks
measured installs/interruption checks and remaining manual cases.

### M10 — Qualify the beginner IDE workflow across projects

Status: firmware handoff prepared; the Swift SDK and IDE are separate repositories.

- [x] Publish the current wire contract and vectors for the separate Swift SDK.
  The SDK should expose discovery/explicit address, identity and target matching,
  console ownership, image transfer/status reconciliation and reconnect. The IDE
  should own editing, compiler integration, board choice, visible upload progress
  and error messages. Firmware does not compile sketches. The
  [SDK handoff](docs/integration/managed-sketch-sdk.md) includes a checksummed
  ZIP export command, shared native-checked state vectors, image metadata,
  pinned proof build recipe, and cross-project test checklist. This completes
  firmware publication, not implementation or consumption in the SDK/IDE.
  The archive, focused test commands/results, and reproduced Blink A capsule
  are recorded in the [handoff evidence](docs/evidence/managed-sketch-sdk-handoff.md).
- [ ] Provide a reproducible reference build recipe for managed Arduino sketches
  with pinned board package, libraries, flags, layout and exported image metadata.
  On macOS, test an IDE/SDK prototype using Arduino CLI; for iPadOS, identify and
  test a separate compatible compilation path rather than assuming a host CLI is
  present. Wrong board/core/layout selections must fail before install.
  The named-proof capsule recipe ran against the exact M9 ELF and reproduced
  the previously tested Blink A hash. Actual IDE compiler integration and a
  compatible iPadOS compilation path remain untested.
- [ ] Run an end-to-end novice sequence on the Nano: initial physical setup,
  first authorized connection where reproducible, Blink upload, console print
  and input, edit/rebuild/reupload, cold unplug/replug and console resume. Record
  whether any step requires BOOTSEL, RESET, network-setting edits or a terminal.
- [ ] Run the same sequence on the available iPad, including locked/unlocked and
  denied-then-allowed accessory states where reproducible. While console traffic
  flows, load an uncached Internet resource over Wi-Fi; record actual route/DNS
  state, exact firmware/image/SDK/IDE versions, cable, times and failures. Do not
  suppress the legitimate host authorization prompt.
- [ ] Give a new user only the setup and IDE instructions, then record observed
  steps, upload/console failures and recovery actions. Distinguish an IDE mock or
  manual test harness from a completed SDK/IDE implementation.

Acceptance: two consecutive IDE-triggered uploads and an exact bidirectional
console session complete on the supported Nano using one cable, without manual
ROM entry after initial installation; both macOS and iPadOS results are labeled
by the paths actually tested. Evidence: cross-project versions, exact images,
commands, host/board reports and a reproducible walkthrough; none yet.

## iPhone/iPad qualification checklist

Use a report in `docs/` for each firmware/host combination. Record model, OS,
board revision, firmware hash, cable/hub, authorization state, time to lease,
expected/observed result, and successes/attempts. Mark unavailable cases untested.

- Cold plug-in and previously authorized reconnection.
- First authorization prompt when reproducible; locked connection then unlock;
  denial followed by reconnection. Do not routinely reset privacy settings.
- Board reset, rapid reconnect, and repeated unplug/replug.
- Automatic IP assignment and HTTP/stream access without manual settings or RESET.
- Internet access over Wi-Fi/cellular while streaming, with router/DNS observations.

For USB/DHCP changes, target 20 cold connections per available target. A recovery
RESET may help debugging but does not turn a failed automatic-startup trial into
a pass. Record desktop packet evidence separately from iOS observations.

## Optional backlog — not committed scope

- [ ] Bonjour/mDNS discovery with numeric-address diagnostics and a considered
  multi-device/subnet-overlap strategy.
- [ ] Runtime/REPL integration and capability-specific runtime controls.
- [ ] File service with filesystem ownership, authorization, and interrupted-write
  behavior defined before implementation.

## Decisions and handoff

Retained decisions: firmware-only product; initial Nano/Philhower CDC-NCM target;
keep option-1 baseline; rejected extra re-enumeration workaround; fix local DHCP
and initial attachment as separately measurable changes. Future changes to these
choices need rationale and evidence, with durable rules updated in `AGENTS.md`.

Historical handoff (2026-09-21; retained for context, superseded by the current
position above): paused repeated attachment at 3/3 by user request;
implemented/tested portable FMGO framing, bounded JSON and application sessions
matching the README proposal. Added bounded two-socket raw-lwIP integration and
a minimal application reference on TCP 7420, with identity-checked smoke tooling.
Initial application images were flashed: bytes/control/ownership checks pass,
but fresh reconnect and a larger transfer failed. Added fixed-slot replacement,
null-PCB handling and explicit lwIP timer dispatch, plus focused regression tests.
The Nano currently runs UART v2. Application v6 previously passed full smoke and
one unretried overlapping Internet replay. V4 previously passed an isolated
replay, overlapping replay, and a
1.25 MiB two-minute sampled memory/queue soak. V2
previously had four unassisted overlap stalls, and v3 stalled on its first isolated
64 KiB replay.
The captured fourth failure implicated the NCM receive continuation. V3 directly
set the pending bit but reproduced the stall on its first isolated 64 KiB replay,
showing that change did not wake the async context. `application-dev-v4` calls the
SDK wake API after releasing the USB mutex, adds persistent NCM/raw-TCP counters,
passes 84 sanitizer-backed native cases and 114 host cases, and builds within
memory budgets. V5 hardware smoke, refined burst and one Internet-overlap replay
pass with a batch peak of 2. V6 adds a USB-mutex-contention counter, passes the same
nine native suites and 114 host cases, and compiles within budget with UF2 SHA-256
`add0366da36c237bb693615911cb0de9ff7d4c8e16908d4386795abba8f94d69`.
Its pass did not exercise the measured contention/budget paths.
Application v7 and UART v2 compile with the reusable UART configuration/backend
boundary. UART v1's initial bounded D1-to-D0 loopback, persisted-state rerun, and
concurrent-Internet check passed, then its first soak failed with measured RX
overrun. V2 is now flashed and passes smoke plus the same unretried 60-second
replay: 458,752 exact bytes, seven sessions, exercised throttling, zero
overrun/loss, stable sampled memory, and Wi-Fi Internet coexistence. An external
Pico peer now passes independent traffic in both directions at 115200/57600 and
controlled overrun/recovery. Its v2 format matrix passes all advertised formats
and bounded baud endpoints; the separate 2 Mbaud burst failure preserves the
no-CTS FIFO limitation. iOS qualification remains open. See
[UART evidence](docs/evidence/uart.md),
[measurements](docs/evidence/application-diagnostics.md)
and [application build/hardware evidence](docs/evidence/application-backend.md).
Opt-in traffic soak now passes its first 60-second Nano run; see
[soak evidence](docs/evidence/application-soak.md). See
[hardware evidence](docs/evidence/application-hardware.md).

Earlier toolchain retry after Xcode/Command Line Tools reinstall: default Git/Clang work;
fresh default-Xcode native sanitizer build and all 22 host tests pass. Firmware
still fails at the Intel-only Arduino ctags helper, including outside the
sandbox. M1 was still blocked at that point; no hardware was contacted. Details are appended to
the foundation evidence report.

Reference starting points: [Arduino-Pico USB Ethernet](https://arduino-pico.readthedocs.io/en/latest/usbethernet.html),
[Arduino-Pico OTA](https://arduino-pico.readthedocs.io/en/latest/ota.html),
[pytest usage](https://docs.pytest.org/en/stable/how-to/usage.html).
These moving references are subordinate to pinned sources and recorded evidence.

Rosetta 2 retry (2026-09-16): ctags executes and the pinned firmware build passes.
Program storage: 113,716 bytes; static RAM: 75,048 bytes; both within budgets.
22 upstream WiFiClient overloaded-virtual warnings remain. Artifact hashes and
command are in the foundation report. Hardware checks remain pending.

Local-only increment: vendored and hardened DHCP, configurable non-overlapping
subnet (default 192.168.77.0/24), isolated clean core-6.0.0 build, 18 native cases,
27 host tests (22 existing + 5 flash gates), and 20,000 mutation cases pass.
ROM-bootloader-only flash command is implemented. One macOS post-flash
coexistence session subsequently passed; see the local-only evidence report.
