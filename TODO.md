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

Production direction accepted on 2026-10-15:
[Nano-first 1.0 gates](docs/integration/production-v1.md) cover mutable sketch RAM,
explicit source bundles, reproducible delivery, normal SDK setup and exact-candidate
host/device/recovery qualification. First implementation: the ordinary
`build --firmware managed` path builds/checks the existing experimental resident,
with failed-build records and CI integration. See [profile](docs/managed-profile.md).
The [local evidence](docs/evidence/managed-profile.md) records 20 sanitizer CTest
targets, 217 host cases, a fresh resident build, exact-commit export and successful
compiler metadata import. Corrective [CI run at `05e8c4b`](https://github.com/AlexGivens/Firmingo/actions/runs/38717838985)
reports all ten jobs successful after CLI PATH resolution; raw remote logs/artifacts
were not retrieved. Local native proof/composite/contract builds also pass.
The [candidate FMS2 contract](docs/integration/stateful-image-v2.md) now has a
portable validator and generation initializer, ten native behavior cases,
independent non-deployable fixture and separate bounded mutation job. The
[allocation/link proof](docs/integration/stateful-link-proof.md) now reserves a
real 4 KiB resident arena, checks candidate API-v2/linker facts and separately
links a stateful ARM counter. Production C++ host initialization validates its
actual capsule. The [runtime increment](docs/integration/stateful-runtime.md) adds
opt-in FMS2 boot and native-tested replacement failure handling. The
[versioned contract and target-2 adapter](docs/integration/stateful-upload-contract.md)
now add a separate upload candidate and exact-commit export. Next: explicit
compiler/SDK adoption and selected-board qualification. Existing ABI-one
restrictions remain intact; no new descriptor or hardware support is offered.
[Local evidence](docs/evidence/stateful-image-v2.md): 21 sanitizer CTest targets,
217 host cases, final ten-case focused proof, five mutation targets and pinned ARM
object compilation pass. [New allocation/link evidence](docs/evidence/stateful-link-proof.md)
records the later real ARM link and RAM allocation; hardware execution remains open.
[CI at `a7966c5`](https://github.com/AlexGivens/Firmingo/actions/runs/38720627833)
reports all ten jobs successful; public statuses inspected, raw remote logs and
artifacts not retrieved. The exact local evidence remains separate.
The later [allocation/link CI at `c2f568b`](https://github.com/AlexGivens/Firmingo/actions/runs/38724190142)
also reports all ten jobs successful, including the new candidate link step;
public statuses inspected, raw remote logs/artifacts not retrieved. Its local
21 sanitizer targets and final 233 host cases pass.
The [runtime evidence](docs/evidence/stateful-runtime.md) records the newer
22 sanitizer targets, 236 host cases and ten lifecycle cases, plus all three
resident modes building locally. [CI at `4e491bc`](https://github.com/AlexGivens/Firmingo/actions/runs/38727327344)
reports all ten jobs successful, including opt-in FMS2 boot; public statuses
inspected, raw remote logs/artifacts not retrieved. No board execution ran.
The [target-2 contract/adapter evidence](docs/evidence/stateful-upload-contract.md)
adds 23 sanitizer targets, 247 host cases, six mutation targets and a canonical
39-file FMS2 export. Initial Linux CI caught a GCC test-formatting warning;
[corrective CI at `97be7bb`](https://github.com/AlexGivens/Firmingo/actions/runs/38810907065)
passes all ten jobs. Raw native/mutation/export logs were inspected; remote archive
bytes were not retrieved. Compiler/SDK adoption and physical qualification remain open.

Compiler/SDK FMS2 adoption now has explicit pinned descriptors and target-2
deployment validation. [Selected Nano setup](docs/evidence/nano-fms2-initial.md)
records the new checked initial composer, full ROM readback, matching Mac hello
and first physical iPad compilation/inspection. The later linked iPad record now
passes both target-2 uploads in one boot, initialized data/BSS, advancing state,
two exact echoes and state after manual RESET. The same sequence now also passes
on the selected iPhone; [both scoped records](docs/evidence/nano-fms2-hardware.md)
retain exact pins and limitations. The same linked record now also pins the
separate iPad and iPhone pre-commit disconnect/power-cycle, real withheld commit
reply, SIGKILL/relaunch query-only recovery and fresh ordinary upload passes;
D2 is removed afterward. The iPhone record retains an unidentified prerequisite
rejection before successful final association. Flash-write power loss, complete
SDK ROM recovery and broader production gates remain open before promotion.

Repository refocus on 2026-10-12 is documented in
[the compiler contract plan](docs/integration/compiler-contract.md) and
[the cross-project migration inventory](../FirmingoKit/docs/compiler-migration.md).
Firmingo retains firmware/ABI/layout, actual build exports, native proof builds,
independent validators, and hardware tests. Firmingo-Compiler owns the portable
WASM pipeline/recipes/artifact packaging and static compiler metadata; FirmingoKit
owns Swift hosting, device evidence and deployment. Portable compiler execution
now lives in Firmingo-Compiler; FirmingoKit consumes
a pinned source-only package through its tested macOS WebKit adapter. Local compiler
tests do not establish Clang-to-resident hardware behavior. Next firmware
integration work now has the experimental exact-build export in M10.1 and independent
validation of two actual Clang capsules against the exact resident API. Complete
upstream provenance and wrapper/hardware qualification remain open. Existing firmware qualification priorities below are retained.

The [selected SDK Nano initial setup](docs/evidence/nano-sdk-initial.md) now
installs and fully verifies the exact exported resident plus the known SDK WASM
GPIO capsule before reboot, then checks one macOS hello. That record covers initial setup.
The later [scoped iPad/iPhone SDK checks](docs/evidence/nano-sdk-hardware.md)
demonstrate console/GPIO uploads, startup, exact bytes and manual LED transitions;
broader USB/Internet/recovery qualification remains open. The offline selected-artifact
composer has focused host tests and is included in the experimental CI build.

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

Status: firmware handoff prepared; compiler, Swift wrapper, and host applications
have distinct ownership. Extraction and cross-project acceptance remain open.

- [x] Publish the current wire contract and vectors for the separate Swift SDK.
  The SDK should expose discovery/explicit address, identity and target matching,
  console ownership, image transfer/status reconciliation and reconnect. The IDE
  should own editing, SDK API calls, board/build choice, visible upload progress
  and error messages. The SDK hosts Firmingo-Compiler; firmware exports its device
  contract and does not compile sketches. The
  [SDK handoff](docs/integration/managed-sketch-sdk.md) includes a checksummed
  ZIP export command, shared native-checked state vectors, image metadata,
  pinned proof build recipe, and cross-project test checklist. This completes
  firmware publication, not implementation or consumption in the SDK/IDE.
  The archive, focused test commands/results, and reproduced Blink A capsule
  are recorded in the [handoff evidence](docs/evidence/managed-sketch-sdk-handoff.md).
- [ ] Provide verified resident facts and versioned ABI/layout inputs for the
  compiler's reproducible Nano recipe. Keep the native GCC proof as independent
  reference; qualify actual WASM capsules through the Swift web-host adapter on
  macOS/iPadOS. Wrong board/core/layout selections must fail before install.
  The named-proof capsule recipe ran against the exact M9 ELF and reproduced
  the previously tested Blink A hash. Actual IDE compiler integration and a
  cross-project iPadOS compilation/deployment path remain untested.
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

### M10.1 — Firmware inputs for the compiler split

Status: experimental exact-build export, independent capsule validation and scoped
iPad two-sketch SDK compatibility demonstrated; production provenance/publication
and broader wrapper/hardware qualification remain open.

- [x] Align root guidance, README, index, handoff and roadmap with compiler/wrapper
  ownership while preserving existing native proof builds and measured evidence.
- [x] Specify actual resident export facts, compatibility rules and independent
  qualification gates in `docs/integration/compiler-contract.md`; record the source
  snapshot in FirmingoKit's migration inventory. This completes planning only.
- [x] Implement a non-flashing exact-build export alongside the existing parser/
  documentation ZIP. Derive hashes/API metadata from actual consistent resident
  ELF/UF2/reports and selected source, with explicit native/composite distinctions.
- [x] Test missing/stale/mismatched/corrupt reports/artifacts, unsafe paths, ambiguous
  selection, provenance and deterministic output where supported; align the local
  entry point, CI and documentation in that implementation increment.
- [x] Export versioned, checksummed ABI/facade/layout source inputs and explain
  exact API/resource compatibility across resident updates; do not make firmware depend on compiler
  package versions or infer exact installed bytes from hello.
- [x] Validate compiler-produced Nano artifacts independently, then qualify two
  sketches through the wrapper in an authorized hardware workflow. Preserve native
  GCC results as separate evidence; Pico managed support remains unimplemented.
  The scoped iPad result is pinned in FirmingoKit's `docs/nano-hardware-ipad.json`:
  both known capsules, RAM verification, one host commit attempt and fresh
  boot-confirmed reconciliation per operation, exact 256-byte console echo and
  manual D13 off-to-on transition after a RESET baseline. Earlier failures remain
  recorded; no timing, Internet routing, cold-attachment matrix, minimum-OS or
  power-loss qualification follows. The equivalent iPhone result is now recorded
  separately in `docs/nano-hardware-iphone.json`; see
  [the cross-project hardware summary](docs/evidence/nano-sdk-hardware.md).

Acceptance: the compiler consumes verified firmware facts without fake addresses
or device access; wrappers separately validate installation/deployment. New exports
and actual compiler/hardware acceptance need their own recorded checks.
Evidence: [experimental export](docs/evidence/resident-compiler-export.md): existing
exact M9 source/ELF/UF2/report export, 152,840 compared flash bytes, 20 sanitizer
native/191 host tests passed. Compiler imports metadata and two actual WASM
capsules pass production firmware validation; a repeated build is byte-identical.
No device operation or new local resident build occurred. The later schema-2
[source contract](docs/integration/resident-source-contract.md) exports six exact
source/license resources; compiler mapping/import and candidate inventory pass.
See [local source-contract evidence](docs/evidence/resident-source-contract.md). CI now produces the
same export for its newly compiled experimental resident; all ten jobs in
[run 38378075416](https://github.com/AlexGivens/Firmingo/actions/runs/38378075416)
at `4d53a28` are inspected passing. The fresh export imports into Compiler,
derives its changed API address and validates two actual WASM capsules;
the stale historical API address correctly rejects the new capsule.

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

Ownership accepted 2026-10-12: Firmingo is authoritative for device contracts and
verified resident facts; Firmingo-Compiler owns portable compiler assets/pipeline;
FirmingoKit owns Swift hosting, associations and deployment. The three repositories
may be changed together for an authorized task, with ArduinoKit read-only and
unrelated work preserved. Existing handoffs/evidence below remain historical;
their reported results are not compiler migration or qualification results.

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

## Selected unfinished-flash qualification — 2026-10-16

- [x] Establish an actual signed macOS SDK ROM transfer baseline, fresh hello/exact echo and independent complete payload readback. This replaces the selected FMS2 resident; it does not qualify first installation from non-Firmingo firmware or sandbox execution.
- [x] Prepare opt-in post-erase and first-page checkpoint residents, exact ELF facts, export rejection and one-shot native harness with durable intent; add both build modes to CI. [Procedure and exact build ledger](docs/integration/flash-checkpoint-qualification.md).
- [x] Qualify the selected post-erase manual power cut: all 4096 managed-sector bytes erased, all resident bytes unchanged, invalid-slot network boot and new full SDK recovery with exact echo/ROM readback.
- [ ] Execute the separate first-page manual power cut, verify the exact incomplete sector/unchanged resident and qualify its new full SDK recovery.
- [ ] Separately qualify SDK host restart after an accepted partial UF2 prefix. Keep host acceptance and actual flash contents distinct.
- [ ] Keep power loss inside a flash-chip command unqualified unless controlled switching/instrumentation becomes available.
