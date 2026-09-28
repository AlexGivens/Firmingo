# Nano managed-sketch proof contract (experimental)

This describes the `m7exp3` proof on the Arduino Nano RP2040 Connect with
Arduino-Pico 6.0.0. It is **not** a public sketch format, an arbitrary Arduino
library environment, or a capability of the 0.1.0 release UF2s. The exact
resident ELF and module capsule must be built together. See the
[Nano hardware record](../evidence/managed-sketch-m7exp3.md) for what ran and
the [Nano layout decision](managed-sketch-nano-layout.md) for flash ownership,
RAM limits, and the update path.

`m8exp1` retained this narrow image format and replaced the resident's ad hoc
byte forwarding with the portable `SketchConsole` adapter. It compiled and
flashed, but its first Nano echo received zero bytes; FMGO input stopped in
the application queue. `m8exp2` handoff diagnostics showed a loop-counter
plateau before the new owner was acknowledged, even though D13 was later seen
blinking. `m8exp3` removed unnecessary atomic read-modify-write operations,
but its 256-byte echo also failed. `m8exp4` moves response scratch off the
small core-0 stack; its first exact-byte Nano console smoke passed. This does
not establish the cause of the previous failures. M8 is not yet accepted or
IDE-verified.

The `m9exp1` candidate carries this constrained module format onto
FMGO via the [experimental upload extension](../protocol.md#experimental-nano-managed-upload-extension-m9).
Its [hardware record](../evidence/managed-sketch-m9exp1-hardware.md) covers
successive network installs, exact console traffic, status reconciliation and
one pre-commit power cut. The M7 private TCP 7421 listener is absent from that
candidate. The [SDK handoff](../integration/managed-sketch-sdk.md) describes the
current build and client integration; the M7 behavior below remains historical.

## Execution and ownership

- The resident owns boot, NCM USB attachment, local-only DHCP, FMGO TCP 7420,
  the private upload listener on TCP 7421, flash writes, and all network
  callbacks on core 0. It does not open USB CDC or the NINA Wi-Fi module.
- One separately linked module occupies the fixed sketch slot beginning at
  `0x10200000`. Its code begins at `0x10200100`. The resident validates the
  board tag, ABI version, exact resident API table address, entry points,
  length, padding, and code SHA-256 before calling it.
- The resident calls the module's `setup()` once for each accepted generation,
  then calls `loop()` repeatedly on core 1. The module must return from each
  call for a live replacement to park core 1. A call that never returns can
  keep the sketch slot executing and prevent network replacement; the resident
  is intended to refuse the write after a one-second park deadline.
- There is one FMGO owner for the application byte stream. A second owner gets
  `busy`. The resident keeps 256-byte bounded queues in each direction and
  discards unsent output when no stream owner is open. Opening or closing a
  stream starts a new input epoch; old input is not replayed to a new owner.

For `m8exp1`, opening the FMGO channel enables sketch output only after the
core-1 sketch reaches its next `loop()` boundary. Output left by the previous
owner is discarded first. Closing disables both directions at the next core-0
poll; reopening starts another epoch. A stalled or nonreturning sketch may
delay that boundary, so the new owner receives no old sketch bytes. The two
bridge queues each hold at most 256 bytes. Full queues return a short or zero
`Serial.write` result and `availableForWrite()` returns zero; no method waits
for the peer. Bytes written before open are refused and counted. Core-1 sketch
input is also withheld until the boundary, then delivered in order and without
line-ending conversion. The four `sketch_*` diagnostics described in
[the FMGO protocol](../protocol.md) separate refused writes from disposed
queued bytes. A physical reset or power cycle clears these boot-lifetime
counters and reloads the valid slot.

## Deliberately small Arduino-shaped facade

The capsule includes the local `facade/Arduino.h`, not the Arduino-Pico core.
It exposes `pinMode`, `digitalWrite`, `millis`, `delay`, and a basic `Serial`
object with `begin`, `available`, `availableForWrite`, `read`, `write`,
`print`, and `println`. `LED_BUILTIN` is Nano D13 / GPIO6. The two Blink
proofs use only D13 output, `millis`, `delay`, and byte-oriented `Serial`.
`Serial.begin(baud)` does not change a UART speed; it is a compatibility no-op
for this network console. `Serial` truthiness means the service exists, not
that a host has opened it. `Serial.write` can be partial, so callers must
check available space or handle a short result. Payload bytes are not
transformed, and pre-open output is discarded.

The resident currently forwards `pinMode(..., OUTPUT)` and `digitalWrite`
for most RP2040 GPIO numbers below 30, while reserving D2/GPIO25 as the
physical upload arm input. Other modes and invalid arguments are silently
ignored by these two `void` facade calls. **Only the D13 usage above has
been exercised and is within this proof's compatibility claim.** The pin API
needs a narrower, explicit error contract before offering arbitrary GPIOs.
The module linker rejects writable `.data` and `.bss`, so mutable globals,
static state, typical Arduino library initialization, interrupts, USB APIs,
NINA Wi-Fi APIs, filesystem writes, direct flash writes, and native `SerialUSB`
are outside the proof. A conventional whole-board UF2 replaces the resident
and is not a managed-sketch capsule.

There is no sketch-only reset API. FMGO `device.reset` returns `unsupported`
without rebooting; this was checked on the recovered Blink B image. A
physical reset or power cycle restarts the resident and loads a valid slot
again, rather than resetting only the module. One normal unplug/replug was
observed; physical RESET behavior on this exact image was not separately
measured.

## Replacement and recovery boundary

The private listener accepts at most one 4 KiB capsule, validates its digest
and exact resident API address, and requires the physical D2-to-GND arm at
intake and commit. It refuses an active FMGO stream during commit. Core 1
must reach the resident's SRAM park point within one second; otherwise the
resident refuses the upload before erasing. Once parked, core 0 erases and
programs only the first 4 KiB sketch-slot sector from RAM, reads back the
installed capsule, and starts the new generation. The installed/readback
response is evidence of this one-sector write; it does not attest to the
module's behavior or prove a healthy reboot.

One A-to-B replacement, same-image reinstall, unarmed refusal, normal
post-replacement power cycle, and one bounded five-second sketch-pause
timeout passed on one Nano. One deliberately nonreturning core-1 sketch
also kept the resident reachable, refused replacement without a slot write,
and required successful ROM recovery. There is no rollback,
power-cut guarantee, recovery from arbitrary sketch faults, general upload target in FMGO,
or iPhone/iPad qualification. A failed or uncertain install is inspected via
the Nano's independent REC/GND ROM path. The mass-storage UF2 copy once
updated the resident while leaving an older sketch slot; a ROM readback
detected this, and an exact `picotool` sector write restored the proof.
Future initial installs must verify the **full** composite before claiming
that the sketch was installed. One exact Blink B full-composite initial
install with pinned `picotool` has now passed resident-plus-slot readback
verification and post-reboot exact-byte smoke on the selected Nano.
