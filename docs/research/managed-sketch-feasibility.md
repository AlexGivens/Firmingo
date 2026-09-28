# Managed sketches on one Nano: feasibility note

Status: design research plus one Nano hardware replacement proof,
2026-09-25. A constrained Blink A image passed ROM-mode readback and network
smoke; a physically armed private installer replaced it with Blink B, which
passed exact echo before and after one normal unplug/replug. This is one
selected-board proof, not a qualified general Arduino sketch runtime. A later
exact Blink B full-composite `picotool` install verified both resident and
sketch ranges before reboot and passed new-boot smoke; one bounded five-second
sketch pause also returned the expected park timeout without replacing the
running sketch. A deliberately nonreturning core-1 sketch kept the resident
network service reachable; the replacement timed out without changing the
slot, and exact Blink B ROM recovery passed.
This note does not change the 0.1.0 release
contract or the ordered acceptance checks in [TODO.md](../../TODO.md#m7--prove-a-persistent-single-board-sketch-architecture).

## What the current artifacts establish

- The Nano 0.1.0 application image is a conventional Arduino sketch. Its
  `setup()` starts DHCP, the TCP service, and NCM; its `loop()` polls the service
  and echoes bytes. Replacing that entire sketch would replace those services.
- The pinned Nano board selection is Arduino-Pico 6.0.0 with 16 MiB flash and
  `flash=16777216_0`: 16,773,120 sketch bytes and a 4 KiB EEPROM reservation.
  The filesystem start and end are equal in this selection, so it has no
  LittleFS staging region. The current build's 1 MiB flash and 192 KiB static
  RAM limits are engineering budgets, not a measured managed-sketch memory map.
- The pinned core archive `rp2040-6.0.0.zip` matches the SHA-256 in
  `boards/nano_rp2040_connect/core-lock.json`. Its `docs/ota.rst` requires a
  LittleFS partition for OTA. `libraries/Updater/src/Updater.cpp` stages
  `firmware.bin` there; the OTA boot code copies staged blocks to flash at
  reboot. The default core `main.cpp` calls one sketch's `setup()` and then
  `loop()` repeatedly. These are whole-sketch mechanisms. They do not themselves
  provide a separate application slot or a fixed interface to a resident
  service.

The existing 0.1.0 UF2s are verified packaged builds, but their exact images
still need the release hardware checks. Prior heap and stack samples apply to
older development images and do not establish space for a managed sketch.

The [experimental Nano build](../../experiments/managed_sketch/README.md) now
compiles a resident and two separately linked, stateless Blink/console modules.
The first hardware-tested resident reports 128,048 flash bytes and 88,768
static RAM bytes; the current upload-capable `m7exp3` resident reports
131,752 and 93,472;
modules A and B have 432 and 436 code bytes. Its ROM-only composite UF2s place
one module in the provisional sketch slot. After the exact Blink A composite
was copied through ROM recovery, the Nano reported the experimental resident
and passed an FMGO/echo check; see the
[first hardware record](../evidence/managed-sketch-initial.md). This establishes
one resident/slot combination. The later
[m7exp3 hardware record](../evidence/managed-sketch-m7exp3.md) shows one
physically armed A-to-B replacement, a readback-success response, exact-byte
traffic, a 500 ms Blink B LED rate, and one normal power-cycle persistence
check. The later pinned-`picotool` full-composite install and bounded-pause
observations are in the same record. They do not establish general Arduino
compatibility or recovery from arbitrary sketch faults.

## Provisional architecture and open decisions

The working candidate is a **Nano-specific resident image plus a constrained
sketch slot**. The resident image would own startup, USB, DHCP, TCP, the console
queues, image validation, and recovery metadata. A separately built sketch
would have a fixed entry interface and limited Arduino API access. Its linker
address, initialization, `setup()`/`loop()` scheduling, interrupts, global state,
and `Serial` mapping all need a documented contract. An arbitrary conventional
UF2 cannot be accepted as a managed sketch image.

The [Nano layout decision](managed-sketch-nano-layout.md) now selects these
owners and ranges for the M7 proof, while only the first sketch-slot sector
is implemented for network replacement. Running sketch work on the second
core might keep the network service responsive during a long `delay()` or loop,
but shared RAM, flash, peripherals, interrupts, and Arduino library assumptions
still need direct testing. A stalled or misbehaving sketch may defeat software
isolation on this MCU. The service must never claim survival of such faults
until measured. A whole-image Arduino-Pico OTA update can be investigated as a
way to replace the combined firmware, but it does not meet the persistent
service requirement by itself.

## Provisional Nano proof layout

This is a **test allocation**, not a public image format or a qualified flash
map. It keeps erase boundaries explicit and leaves room to revise sizes after
linker and hardware measurements. Addresses are RP2040 XIP addresses; end
addresses are exclusive.

| Region | Start | End | Size | Intended owner |
| --- | --- | --- | ---: | --- |
| Resident boot and Firmingo | `0x10000000` | `0x10200000` | 2 MiB | ROM entry, service, recovery logic |
| Managed sketch | `0x10200000` | `0x10600000` | 4 MiB | One board-matched sketch image |
| Raw staging | `0x10600000` | `0x10a00000` | 4 MiB | Uncommitted image bytes |
| Metadata | `0x10a00000` | `0x10a10000` | 64 KiB | Bounded install state and validity records |
| Unassigned | `0x10a10000` | `0x10fff000` | remainder | No writes in the proof |
| Existing EEPROM reservation | `0x10fff000` | `0x11000000` | 4 KiB | Existing board convention |

The 0.1.0 Nano linker map places `.boot2` at `0x10000000`, its OTA/partition
area before `.text`, and `.text` at `0x10003000`. The stock linker declares one
FLASH region starting at `0x10000000`; it has no independently executable
sketch region. A prototype therefore needs an explicit linker and startup
contract for the slot. It must also make every installer write reject the
resident and EEPROM ranges. Reserving 2 MiB for the resident does **not** show
that its future code will fit, and a 4 MiB slot does not establish library
compatibility.

The build report for the current Nano application lists 125,968 program bytes
and 88,192 static RAM bytes. These are size baselines for the existing reference
only. The linker exposes 256 KiB main RAM plus two 4 KiB scratch banks; resident
and sketch globals, stacks, heap, lwIP pools, and any cross-core buffers need a
measured joint budget before declaring a managed-sketch limit.

## Boot and scheduling questions for the proof

- The resident must start NCM and its diagnostic endpoint even when no sketch
  slot is valid. It should call a slot entry only after validating the image
  format, range, ABI, and recorded install state. A failed sketch must leave a
  clear status for the next connection. No image is automatically replayed or
  reinstalled after reconnect.
- The pinned core has optional `setup1()`/`loop1()` support on the second core,
  but its normal startup discovers those functions at link time. A separately
  linked sketch needs a deliberate handoff and ownership contract. The first
  prototype should compare a cooperative same-core call with a second-core
  call under `delay()`, long loop work, and a stalled sketch. Only the measured
  result can determine the supported scheduling and Arduino API subset.
- RP2040 flash programming interrupts execution from external flash and
  involves both cores. The installer must keep USB/lwIP callbacks free of flash
  work, coordinate the sketch core, and bound network disruption. Stage and
  verify before touching the active slot. If power is lost while copying into
  the slot, the resident should still boot and report an invalid sketch; this
  is a design target, not a tested guarantee or rollback claim.
- The pinned Arduino-Pico `main.cpp` calls `rp2040.fifo.registerCore()` on core
  1, installing its exclusive SIO FIFO IRQ handler. The SDK's
  `flash_safe_execute_core_init()` uses
  `multicore_lockout_victim_init()`, which also installs an exclusive handler
  for that IRQ. These paths cannot simply be enabled together. Arduino-Pico's
  EEPROM and Updater code instead uses `rp2040.idleOtherCore()` before flash
  operations, but that call waits without a bounded failure result. More
  fundamentally, a paused core cannot return into the old sketch slot after
  that slot is erased. A network installer needs a verified core-1
  quiesce/handoff protocol before erase, and a bounded refusal path when the
  sketch cannot reach it. The first hardware check performed no flash write
  from running firmware.

A later resident candidate adds `Core1Pause`, a cooperative request/ack
gate called at the resident-owned `loop1()` boundary. The pinned compiler put
its 58-byte park function at `0x200000c0` in SRAM with no function calls or
PC-relative literal loads in its disassembly. Native sanitizer tests cover
request, cancellation, and 100 repeated handshakes. One same-image network
reinstall and one A-to-B replacement have now exercised the gate and one-sector
flash writer on the selected Nano. A long or stalled sketch may never acknowledge; an
installer must then time out without writing flash.

- `Serial` in a managed sketch will need a service-owned byte interface. The
  pinned core's `Arduino.h` includes `SerialUSB.h`, whose `SerialUSB Serial`
  object is a USB CDC endpoint. Its `SerialUSB::begin()` disconnects USB,
  registers CDC endpoints, and reconnects. Thus an ordinary sketch calling
  stock `Serial.begin()` would violate the intended NCM-only attachment. The
  managed-sketch build needs a deliberate `Serial` facade or a reviewed core
  port, with a narrow API and tests; `DISABLE_USB_SERIAL` alone is insufficient
  because it only suppresses the core's automatic `Serial.begin()`. The proof
  should specify what pre-open output does and whether normal sketch startup
  proceeds without an IDE connection. UART hardware controls remain separate
  from this network console.

The experimental resident includes a 4 KiB `CapsuleStage`, a one-sector
writer, a switch coordinator, and a private upload listener. The stage rejects unauthorized starts, wrong
owners and offsets, incomplete or oversized input, digest mismatches, invalid
image headers, and trailing bytes. The writer hardcodes the first sketch-slot
sector, pads 256-byte pages from RAM, and checks readback. The coordinator
waits at most one second for core 1 to reach its SRAM park point, stops the old
generation before erase, and starts the new generation only after readback.
Its native tests inject stalls and flash failures. The Nano port masks core-0
interrupts during erase/program. The private TCP 7421 parser accepts one
bounded capsule, checks the selected device ID and digest, and requires Nano
D2 held low at the start and commit. D2 is reserved from the sketch facade.
One same-image reinstall and one A-to-B replacement have now used this path
on the selected Nano, with readback-success responses and exact echo afterward.
This RAM size is a proof limit, not a general sketch capacity, and D2 is a
physical authorization mechanism for this experiment. Deliberately stalled
sketches, interrupted flash operations, and repeated cold cycles remain
untested.

## Smallest decision-making proof

1. Reserve nonoverlapping resident, sketch, staging, metadata, and EEPROM areas
   on the Nano; record actual linker output, alignment, erased-sector bounds,
   RAM use, and what executes from flash during writes. Keep ROM recovery intact.
2. Boot a resident service and one minimal Blink/console sketch. Install a
   *different* sketch into the sketch slot and show that USB Ethernet, identity,
   and the console remain available after the switch. Record exact images and
   sizes, boot observations, and recovery steps.
3. Repeat with normal `delay()`, a long loop, cold reconnect, and a deliberately
   stalled sketch. State precisely which failures the service survives. If the
   desired Arduino API subset cannot coexist with a responsive service, narrow
   the sketch contract or assess a separate persistent bridge/programmer before
   specifying network upload commands.

Only after this proof should M8 console semantics and M9's target/image format
be frozen. Pico's 2 MiB layout is a separate later decision.

## Replacement handoff required by the first hardware result

The first Nano check ran a stateless Blink A module from the sketch slot while
the resident served exact FMGO traffic. It did not write flash from running
firmware. A replacement proof must meet these conditions before any live slot
erase:

1. Receive and validate the complete board-matched capsule in a separate
   staging buffer. The current proof uses 4 KiB of RAM, not the provisional
   raw-flash staging region. Keep the active slot and resident untouched on malformed,
   unauthorized, or incomplete input. Do not advertise an FMGO target until
   authorization, status, and these storage operations are implemented.
2. Ask core 1 to return from the module's `loop()` to a resident-owned safe
   point. A bounded timeout must refuse replacement if a sketch remains in a
   long or stalled call. The safe point must park core 1 in **RAM** while flash
   is busy; a flash-resident wait loop is insufficient.
3. On core 0, copy each source page into RAM before programming. Flash cannot
   supply the source buffer while XIP is unavailable. Keep flash operations
   outside USB/lwIP callbacks and verify the entire active image before
   releasing core 1. The scheduler must enter the new module's `setup()`;
   returning to the old slot after erase would execute invalid code.
4. Treat a lost response or power cut as an uncertain install. The next boot
   must still start the resident and report the actual validated slot state.
   A valid staging image must not be replayed automatically. ROM recovery
   remains the independent fallback; no rollback has been proved.

The A-to-B Nano proof exercised steps 1–3 once, including core-1 parking and
readback before resuming the sketch. Step 4 remains only partly observed: one
normal post-install power cycle preserved Blink B, while interrupted writes
and recovery from an invalid slot have not been tested. The proof's
cooperative park avoids the pinned core's conflicting FIFO lockout handler
and avoids the unbounded `idleOtherCore()` wait; it has not established
survival of a stalled or faulty sketch.
