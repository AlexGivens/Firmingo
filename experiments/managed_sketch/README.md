# Experimental Nano managed-sketch modules

This directory is an M7–M9 **experimental** proof path. Current source builds
the `m9exp1` FMGO upload candidate described below. It is outside the 0.1.0
release package; release references still omit the upload target. The experimental
resident validates and can call one XIP-linked sketch module on core 1, while
core 0 owns NCM, DHCP, FMGO, and bounded console mailboxes. One exact Blink A
image passed an [initial Nano check](../../docs/evidence/managed-sketch-initial.md).
The `m7exp3` candidate adds a private, physically armed upload listener and
corrects the Nano D2 mapping. Its first composite UF2 copy updated the resident
but left an old sketch capsule in flash; an exact ROM-mode slot write and
readback restored Blink A, which passed the binary echo smoke. With D2
grounded, one same-image A reinstall and one A-to-B network replacement both
returned readback success and passed exact echo on the selected Nano. See the
[m7exp3 evidence](../../docs/evidence/managed-sketch-m7exp3.md).
There is no general Arduino compatibility claim.

## M9 FMGO upload candidate

The `m9exp1` candidate carries one 4 KiB capsule target onto FMGO TCP 7420
and removes the private TCP 7421 listener from this resident. It retains
the constrained Nano capsule/facade and physical D2/GPIO25-to-GND arm.
`flash.begin`, type-4 chunks, finish, commit, abort, and status use the
[experimental protocol extension](../../docs/protocol.md#experimental-nano-managed-upload-extension-m9).
Commit queues work; the main loop parks core 1 and installs outside the
network lock. A new-generation acknowledgement distinguishes installed
readback from setup/first-loop return. No rollback or persistent status journal
is implemented. The [build record](../../docs/evidence/managed-sketch-m9exp1.md)
contains exact hashes, native/host results, memory sizes, and limitations.

The selected Nano is now on M9 at the user's request. Initial ROM readback,
successive A/B network installs, exact console smoke, pre-commit disconnect,
rejection checks, and status reconciliation after a discarded commit result
are in the [hardware record](../../docs/evidence/managed-sketch-m9exp1-hardware.md).
Both LED half-periods were observed. One power cut before commit preserved
the installed sketch and allowed a fresh network upload; power loss during
erase/program remains untested. Do not
restore the preserved M8 image until the user asks. The commands below require
explicit physical setup/testing and are separate from smoke.

Rebuild into an isolated directory with the pinned overlay:

```sh
PROOF_CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli'
PROOF_CONFIG=build/firmware/nano_rp2040_connect/application/usb-toolchain/arduino-cli.yaml
PROOF_TOOLS=build/firmware/nano_rp2040_connect/application/usb-toolchain/data/packages/rp2040/tools/pqt-gcc/5.0.0-9576866/bin
PROOF_BUILD=build/managed_sketch/m9exp1
.venv/bin/python experiments/managed_sketch/build_resident.py --cli "$PROOF_CLI" --config-file "$PROOF_CONFIG" --output-dir "$PROOF_BUILD/resident"
.venv/bin/python experiments/managed_sketch/build_module.py --sketch blink_a --resident-elf "$PROOF_BUILD/resident/artifacts/firmingo_managed_resident.ino.elf" --toolchain-bin "$PROOF_TOOLS" --output-dir "$PROOF_BUILD"
.venv/bin/python experiments/managed_sketch/build_module.py --sketch blink_b --resident-elf "$PROOF_BUILD/resident/artifacts/firmingo_managed_resident.ino.elf" --toolchain-bin "$PROOF_TOOLS" --output-dir "$PROOF_BUILD"
.venv/bin/python experiments/managed_sketch/build_composite.py --sketch blink_b --resident-dir "$PROOF_BUILD/resident" --module-dir "$PROOF_BUILD" --output-dir "$PROOF_BUILD/composite"
```

Recorded build hashes identify the existing artifacts; a rebuild may produce
new hashes and must be reviewed from its new reports. Resident budgets are
1 MiB flash / 192 KiB static RAM; capsules are capped at 4096 bytes. The linker
still forbids writable module data. These limits do not establish dynamic
stack/heap headroom or general Arduino library support.

For a later initial setup, select the recoverable Nano, enter ROM with its
documented REC/GND procedure, and read its flash ID with the pinned picotool.
Only the previously recorded proof Nano has ID `5031503337360009`. This exact
existing Blink B composite is prepared for full readback verification:

```sh
PROOF_PICOTOOL=build/firmware/nano_rp2040_connect/application/usb-toolchain/data/packages/rp2040/tools/pqt-picotool/5.0.0-9576866/picotool
"$PROOF_PICOTOOL" info -a
.venv/bin/python experiments/managed_sketch/flash_initial.py \
  --board nano_rp2040_connect --sketch blink_b --picotool "$PROOF_PICOTOOL" \
  --expected-flash-id 5031503337360009 \
  --expected-uf2-sha256 f33d894bb9ed02ddf410cff36550cecd217c889d0b9c95a5f6f1244e121ca4aa \
  --resident-dir build/managed_sketch/m9exp1/resident \
  --module-dir build/managed_sketch/m9exp1 \
  --output-dir build/managed_sketch/m9exp1/composite
.venv/bin/python experiments/managed_sketch/inspect_hello.py --address 192.168.77.1
.venv/bin/python experiments/managed_sketch/smoke.py --address 192.168.77.1 --device-id 5031503337360009 --sketch blink_b --resident-version m9exp1
```

Compare identity/version with the selected exact install before network
replacement. The version string is self-reported, not remote cryptographic
attestation of the resident UF2. Only after that setup, physically arm D2/GND
and opt into two successive managed replacements (A then B):

```sh
PROOF_BUILD=build/managed_sketch/m9exp1
PROOF_RESIDENT_SHA=658ce6e056e88d3f1d0dfdd4a936263676eaee16d4eb2271b3c400a8c2fb95bb
.venv/bin/python experiments/managed_sketch/fmgo_upload.py \
  --board nano_rp2040_connect --address 192.168.77.1 --device-id 5031503337360009 --target-id 1 transfer \
  --sketch blink_a --resident-dir "$PROOF_BUILD/resident" --module-dir "$PROOF_BUILD" \
  --expected-resident-uf2-sha256 "$PROOF_RESIDENT_SHA" \
  --expected-capsule-sha256 4e92dbd5a9d24952c712bd32d8348a6d0a3de5c54441e42bd8908822564ad3b7 --install
.venv/bin/python experiments/managed_sketch/fmgo_upload.py \
  --board nano_rp2040_connect --address 192.168.77.1 --device-id 5031503337360009 --target-id 1 transfer \
  --sketch blink_b --resident-dir "$PROOF_BUILD/resident" --module-dir "$PROOF_BUILD" \
  --expected-resident-uf2-sha256 "$PROOF_RESIDENT_SHA" \
  --expected-capsule-sha256 6b5ce8500f31ee623a04d4a5264e5543281cd569ab5149f7c30e54712d518a81 --install
```

The harness never resets/flashes in smoke and never automatically replays
commit. Lost replies trigger read-only status reconciliation on the same boot;
a changed boot or superseded record makes the old outcome uncertain. `--stage-only`
in place of `--install` deliberately closes after RAM verification and checks
that the stage was aborted without touching the slot. `status --upload-id N
--boot-id HEX` is read-only and requires the recorded device/boot context.
Both modes have `--help`. A transaction's `boot_confirmed` means setup and
one loop returned, not that the expected LED rate or byte stream is correct.
Observe Blink A's 250 ms / Blink B's 500 ms half-period and run exact console
smoke separately after each replacement. The resident boot ID is expected to
stay the same across managed replacements; upload IDs identify the transactions.

Power-cut/interrupted-flash runs are separate explicit manual hardware tests,
not part of these commands, default smoke, or CI. Preserve REC/GND ROM recovery
and the exact M8 recovery UF2 under ignored
`private-evidence/originals/m8exp4-recovery/`. The overnight M8 result, M9 hardware
acceptance, and cross-project SDK/IDE workflows remain pending.

For integration in the separate SDK/IDE, use the
[M9 SDK handoff](../../docs/integration/managed-sketch-sdk.md), which includes the
portable contract/vector archive command and pinned capsule build recipe.

## M8 console candidate

`m8exp1` added a portable, bounded sketch-console bridge with core-1 loop
boundary handoff and four additive `sketch_*` diagnostic counters. Pre-open
`Serial.write` returns zero and increments `sketch_output_rejected`; closing or
changing owner discards queued bridge bytes. A full queue returns a short
write without blocking the sketch. `Serial.begin` remains a baud-independent
compatibility call, and `Serial` truthiness never waits for an IDE. See the
[contract](../../docs/research/managed-sketch-contract.md) for exact behavior.
Native tests and a pinned Nano compile passed. Its exact image was flashed
and verified, but the first hardware echo received zero bytes. See the
[failure record](../../docs/evidence/managed-sketch-m8exp1.md); do not treat
`m8exp1` as an accepted byte-console image.

The M8 build is isolated under `build/managed_sketch/m8exp1/`. Its resident
uses 132,680 flash bytes and 93,936 static RAM bytes. The resident UF2 SHA-256
is `792a3646e762d617f04cc29495e752d108cef792f099bf889514fdb3121d2824`;
its matching Blink B capsule SHA-256 is
`3d1ecd77a3920d2bc9a517b434109bf638f8b441f544ae10ed7ff9741b09d21c`.
The exact full-composite Blink B UF2 SHA-256 is
`c3f2f41537c76d1836aab7cd9f41556a455d01f722068ec04db201b919feab9c`.
The old M7 Blink B recovery composite remains in ignored
`private-evidence/originals/m7exp3-recovery/blink_b-initial-rom-only.uf2`
with SHA-256 `66b3cc527ffc175dc086e25adf6ed99c1bbae2d4be7036cb110701075294eb05`.

The `m8exp2` diagnostic image showed a loop-counter plateau before owner
handoff, although D13 was later reported blinking; see its
[diagnostic run](../../docs/evidence/managed-sketch-m8exp2.md). `m8exp3`
removed unnecessary atomic read-modify-write operations, but its 256-byte echo
also timed out. The [m8exp3 record](../../docs/evidence/managed-sketch-m8exp3.md)
identifies a large core-0 response-formatting stack frame. `m8exp4` moves the
bounded buffers into Session storage and has a 2,036-byte dispatch frame,
below M7's 2,764 bytes. Its exact-byte Nano console smoke passed before and
after one cold replug, and a concurrent macOS Wi-Fi Internet check passed.
One five-minute host-sleep/wake cycle also retained the boot and passed smoke.
The earlier failure cause and overnight reliability remain unresolved. Its
exact image SHA-256 is `545967f1606f0c366c6bba57640ccea68e620c6ad23463534da328cd82722bea`,
preserved under ignored `private-evidence/originals/m8exp4-recovery/`.
Current M9 source does not reproduce M8; the old source-hash checks correctly
refuse regeneration against changed source. Use the historical exact artifact
only with separate ROM identity/hash/readback checks when recovery is needed.
For the already running M8 image, select `--resident-version m8exp4` with `smoke.py` and
inspect the new diagnostic fields if echo stalls. See the
[first passing M8 console record](../../docs/evidence/managed-sketch-m8exp4.md).
Actual IDE-client reconnect, a connection held open across host sleep, and
overnight checks still need separate hardware observations. One cold replug,
short sleep cycle, and Wi-Fi coexistence pass do not qualify repeated attachment
or iPhone/iPad operation.

The two `.ino` sketches use a deliberately small Arduino-shaped facade:
`pinMode`, `digitalWrite`, `millis`, `delay`, and basic `Serial` byte and
string calls. The modules have no writable globals or Arduino core startup.
The linker rejects `.data` and `.bss`; unsupported libraries will fail
compilation or linking. `Serial.begin(baud)` is a compatibility call for the
network console and does not set a UART speed. `Serial` truthiness means the
resident service is ready; it does not wait for an IDE connection.
The proof echo loops check `Serial.availableForWrite()` before consuming an
input byte. A module that ignores partial `write()` results may still lose
output. The M8 candidate adds bridge counters; general serial compatibility
remains open.

An image has a 64-byte header, zero padding to offset 256, then code linked at
`0x10200100` in the proposed Nano sketch slot. The header records the exact
resident API table address and SHA-256 of code bytes. The experimental
`image.cpp` validator checks these and bounded Thumb entry points before
the resident can execute the image. SHA-256 detects corruption; it does not
authorize an uploader. Mutable module globals are forbidden by the linker.

Build with the pinned compiler and patched Arduino-Pico 6.0.0 overlay from a
verified reference build. From the repository root:

```sh
TOOL_BIN=build/firmware/nano_rp2040_connect/application/usb-toolchain/data/packages/rp2040/tools/pqt-gcc/5.0.0-9576866/bin
ARDUINO_CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli'
OVERLAY=build/firmware/nano_rp2040_connect/application/usb-toolchain/arduino-cli.yaml
.venv/bin/python experiments/managed_sketch/build_resident.py --cli "$ARDUINO_CLI" --config-file "$OVERLAY"
RESIDENT_ELF=build/managed_sketch/resident/artifacts/firmingo_managed_resident.ino.elf
.venv/bin/python experiments/managed_sketch/build_module.py --sketch blink_a --resident-elf "$RESIDENT_ELF" --toolchain-bin "$TOOL_BIN"
.venv/bin/python experiments/managed_sketch/build_module.py --sketch blink_b --resident-elf "$RESIDENT_ELF" --toolchain-bin "$TOOL_BIN"
.venv/bin/python experiments/managed_sketch/build_module.py --sketch pause_probe --resident-elf "$RESIDENT_ELF" --toolchain-bin "$TOOL_BIN"
.venv/bin/python experiments/managed_sketch/build_module.py --sketch stall_probe --resident-elf "$RESIDENT_ELF" --toolchain-bin "$TOOL_BIN"
.venv/bin/python experiments/managed_sketch/build_composite.py --sketch blink_a
.venv/bin/python experiments/managed_sketch/build_composite.py --sketch blink_b
```

The [first hardware record](../../docs/evidence/managed-sketch-initial.md)
identifies the tested 128,048-byte resident and Blink A UF2. That exact image
is preserved under ignored `private-evidence/originals/m7-initial-2026-09-24/`.
The historical `m7exp3` resident has a cooperative core-1 pause gate,
generation-based scheduler, RAM-only capsule stage, one-sector flash writer,
switch coordinator, and private TCP 7421 upload listener. It reports 131,752
flash bytes and 93,472 static RAM bytes; its 58-byte park function is linked
at SRAM address `0x200000c0`. The resident UF2 SHA-256 is
`3986a88b7fbe57fad389304dcd66ade38b59a7f015e3947472d3bafb71af5eb7`.
The generated JSON report contains source hashes. One selected Nano has
executed Blink A and Blink B through this resident; one same-image reinstall
and one A-to-B network replacement passed exact echo. The stage accepts at
most 4,096 capsule bytes,
one owner, and sequential chunks, then checks the whole transfer digest and
image validator. The writer only erases the first 4 KiB sketch-slot sector,
programs 256-byte pages from RAM, and checks readback. A stalled sketch times
out before any write; a failed write stops the old sketch instead of resuming
erased code. D2 is reserved as an input with pullup; connecting D2 to GND
physically arms the private listener. The arm must remain present through the
upload. A 16-digit device ID and exact capsule hash protect board selection
and transfer integrity, but the physical jumper is the proof's authorization
mechanism. This is open development firmware, not a signed update product.

The module builder takes `firmingo_sketch_api` from the **exact** resident
ELF. The composite builder checks hashes and UF2 address bounds, then places
one capsule in the sketch slot. Its two generated UF2s are for **ROM-only
initial proof setup**, not for replacing a sketch over the network. Both
modules have now run on the selected Nano, with Blink B surviving one normal
power cycle. Use the separate experimental flash command below; the experimental
smoke checks the FMGO stream and the distinct resident identity after an
explicit board selection.

## Opt-in first-board check

Use a **recoverable Nano RP2040 Connect** selected by the operator. The ROM
bootloader's `RPI-RP2` Board-ID is generic to RP2040 and cannot distinguish a
Nano from a Pico. Follow the Nano [REC/GND recovery steps](../../docs/usb-startup.md)
to mount the ROM volume. The experimental copy command requires both the
selected mount and a reviewed exact UF2 hash; it regenerates and checks the
composite before writing. For the **corrected m7exp3 candidate**, the exact
Blink A composite hash is in
`build/managed_sketch/composite/blink_a-report.json`:

```sh
.venv/bin/python experiments/managed_sketch/flash_initial.py \
  --board nano_rp2040_connect --sketch blink_a --mount /Volumes/RPI-RP2 \
  --expected-uf2-sha256 c2e48dafe8555c1ef1f69b3c177c97de3078ce21657c057f9804c0dfaeb40a40
```

This **replaces the board's current firmware**. It is not part of smoke or CI.
On the tested Nano, this mass-storage copy updated the resident but left the
old sketch slot intact. A successful copy and resident identity therefore do
not establish that the composite installed. Return to ROM mode and use the
pinned `picotool verify` against the exact composite before claiming the
sketch is installed. The [m7exp3 evidence](../../docs/evidence/managed-sketch-m7exp3.md)
records the first mismatch, a direct 4 KiB slot repair with `picotool load -v`,
and the independent readback and full-composite verification.

For a ROM-mode initial install, use the pinned `picotool` path. It checks the
selected Nano's ROM flash ID, loads the
exact composite with built-in verification, verifies the full UF2 again, and
reboots only after both checks pass:

```sh
PICOTOOL=build/firmware/nano_rp2040_connect/application/usb-toolchain/data/packages/rp2040/tools/pqt-picotool/5.0.0-9576866/picotool
.venv/bin/python experiments/managed_sketch/flash_initial.py \
  --board nano_rp2040_connect --sketch blink_b --picotool "$PICOTOOL" \
  --expected-flash-id YOUR_16_HEX_DIGIT_FLASH_ID \
  --expected-uf2-sha256 66b3cc527ffc175dc086e25adf6ed99c1bbae2d4be7036cb110701075294eb05
```

This exact Blink B full-composite `picotool` path passed on one selected Nano:
both resident and sketch ranges verified, and new-boot exact-byte smoke
passed. The earlier mass-storage copy remains unverified. The script issues no
reboot when load or verification fails, leaving the outcome for inspection.
After the ROM volume ejects and the device acquires an address, read its
self-reported FMGO hello and compare the device ID with the selected Nano's
prior board record. A reported ID alone cannot establish physical identity:

```sh
.venv/bin/python experiments/managed_sketch/inspect_hello.py --address 192.168.77.1
```

Then run the nondestructive experimental smoke with that verified ID. Select
the `m7exp3` resident generation, so the earlier `m7exp1` and `m7exp2` images
cannot pass as this candidate:

```sh
.venv/bin/python experiments/managed_sketch/smoke.py \
  --address 192.168.77.1 --device-id YOUR_16_HEX_DIGIT_ID \
  --resident-version m7exp3 --sketch blink_b
```

Record the exact UF2 hash, observed LED rate (B: 500 ms half-period), device
and boot IDs, network result, echo counts, and the actual REC/GND recovery
outcome. The host smoke verifies FMGO identity and exact bytes; the current
protocol cannot attest which sketch capsule occupies the slot. Do not infer a
sketch switch from the host test alone. A failed or detached application can be
recovered through the independent Nano ROM path.

## Physically armed network replacement

The old `m7exp2` image used numeric GPIO2 as the upload arm input, while Nano
D2 is GPIO25. The corrected `m7exp3` resident and Blink A capsule now pass
ROM readback and exact-byte smoke on the selected Nano. To test the private
network replacement, connect **Nano D2 to GND** with a
jumper for a private network replacement; do not connect REC to GND for that
network step. Leave D2 grounded until the command returns. This will erase
the first sketch-slot sector, but the resident remains in its separate flash
region:

```sh
.venv/bin/python experiments/managed_sketch/upload.py \
  --board nano_rp2040_connect --address 192.168.77.1 \
  --device-id YOUR_16_HEX_DIGIT_ID --sketch blink_a \
  --expected-resident-uf2-sha256 3986a88b7fbe57fad389304dcd66ade38b59a7f015e3947472d3bafb71af5eb7 \
  --expected-capsule-sha256 68516001acf84205a9af84e497aae598ba3be1c192188da5c1fc32ca361d609d \
  --install
```

The selected Nano passed this same-image reinstall and the separate Blink B
replacement shown below. Keep D2 grounded for that command too.

The client verifies the selected board's FMGO hello, local resident and module
build reports, source hashes, exact capsule hash, and a 4 KiB size limit before
contacting the private upload port. It sends a single versioned frame with the
full SHA-256 digest and device ID, then closes the TCP write side. The resident
requires that half-close so trailing bytes can be rejected before any write.
It validates the capsule in RAM, waits at most one second for core 1 to park,
then replaces only the first
sketch-slot sector. Its single-byte response is `K` for readback success, `D`
for missing physical arm, `B` for an active FMGO stream, `I` for invalid input,
`T` for timeout, or `F` for install failure. A lost response leaves the outcome
uncertain; inspect the board before retrying. The command then checks the same
boot ID and 4 KiB of exact FMGO echo data. It cannot remotely attest LED rate.

```sh
.venv/bin/python experiments/managed_sketch/upload.py \
  --board nano_rp2040_connect --address 192.168.77.1 \
  --device-id YOUR_16_HEX_DIGIT_ID --sketch blink_b \
  --expected-resident-uf2-sha256 3986a88b7fbe57fad389304dcd66ade38b59a7f015e3947472d3bafb71af5eb7 \
  --expected-capsule-sha256 f9824db71be8536d952f9e4ef95476303aab76c68afa36b1cbc02b8405ed4659 \
  --install
```

Remove the D2 jumper after the command. On the selected Nano, the user
observed Blink B's approximately 500 ms half-period before and after one
normal unplug/replug, and the post-reboot full binary smoke passed. A
header-only upload attempt with D2 disconnected returned `D`. Only one
selected Nano has exercised the listener and flash coordination. A bounded
five-second pause was also tested below; permanent stall, power-cut, repeated
cold, and iOS checks remain open. The only available recovery claim remains
the Nano's independent ROM mode. Do not use the default release flash command
for this experiment.

## Bounded-pause check (run on one Nano)

`pause_probe.ino` reserves byte `0xf0`: it replies `0xf1`, then spends five
seconds in `delay()` before returning to the resident-owned core-1 boundary.
Byte `0xf2` replies `0xf3` without pausing. Other bytes echo. Its D13
half-period is 375 ms. This is a diagnostic sketch, not a transparent serial
application. Its code is 440 bytes and its exact 696-byte capsule SHA-256 is
`5641ec39766c29b5c29f44b26492285a3e7e7c5eee012b7f368e1c28a9d67887`.

On a deliberately selected, ROM-recoverable Nano with **D2 bridged to GND**,
install the diagnostic sketch through the private listener:

```sh
.venv/bin/python experiments/managed_sketch/upload.py \
  --board nano_rp2040_connect --address 192.168.77.1 \
  --device-id YOUR_16_HEX_DIGIT_ID --sketch pause_probe \
  --expected-resident-uf2-sha256 3986a88b7fbe57fad389304dcd66ade38b59a7f015e3947472d3bafb71af5eb7 \
  --expected-capsule-sha256 5641ec39766c29b5c29f44b26492285a3e7e7c5eee012b7f368e1c28a9d67887 \
  --install
```

The separate opt-in probe sends the trigger, closes the FMGO stream, and
sends a complete Blink B upload while the sketch is inside `delay()`. It
expects the resident's one-second park timeout (`T`), then checks that the
same diagnostic sketch resumes, remains on the same boot, and echoes normal
bytes. It does not run in default smoke:

```sh
.venv/bin/python experiments/managed_sketch/probe_pause.py \
  --address 192.168.77.1 --device-id YOUR_16_HEX_DIGIT_ID \
  --expected-resident-uf2-sha256 3986a88b7fbe57fad389304dcd66ade38b59a7f015e3947472d3bafb71af5eb7 \
  --expected-pause-capsule-sha256 5641ec39766c29b5c29f44b26492285a3e7e7c5eee012b7f368e1c28a9d67887 \
  --expected-blink-b-capsule-sha256 f9824db71be8536d952f9e4ef95476303aab76c68afa36b1cbc02b8405ed4659 \
  --run
```

After the probe, restore Blink B with the earlier `upload.py --sketch blink_b`
command while D2 remains grounded, then remove the jumper. If any step has
an uncertain outcome, inspect the resident and use the independent Nano ROM
path before retrying. This bounded pause is **not** a permanent-stall or
power-cut test. The [m7exp3 evidence](../../docs/evidence/managed-sketch-m7exp3.md)
records one Nano run: response `T` in 1.019 s, same boot and continued
diagnostic-sketch echo, followed by successful Blink B restoration and full
exact-byte smoke.

## Permanent-stall check (run and recovered on one Nano)

`stall_probe.ino` is a deliberately destructive-to-the-running-sketch
diagnostic. It normally echoes bytes and blinks D13 with a 625 ms
half-period. Byte `0xf6` replies `0xf7` to identify the module. Byte `0xf4`
replies `0xf5`, sets D13 high, then runs an infinite core-1 loop that never
reaches the resident's park point. The disassembled loop is a `nop` followed
by a backward branch in the sketch slot. The 696-byte capsule SHA-256 is
`39392336669eaae56d34a7350eda82e85cc5ce41ae4ea2dbcb5a8195239b917b`.
Triggering it requires **physical REC/GND ROM recovery** to restore a sketch;
network replacement should time out before touching flash.

First keep the known recovery image available: exact Blink B composite SHA-256
`66b3cc527ffc175dc086e25adf6ed99c1bbae2d4be7036cb110701075294eb05`.
The selected Nano must be recoverable, running `m7exp3`, and physically armed
with D2-to-GND. Install the diagnostic module without triggering it:

```sh
.venv/bin/python experiments/managed_sketch/upload.py \
  --board nano_rp2040_connect --address 192.168.77.1 \
  --device-id YOUR_16_HEX_DIGIT_ID --sketch stall_probe \
  --expected-resident-uf2-sha256 3986a88b7fbe57fad389304dcd66ade38b59a7f015e3947472d3bafb71af5eb7 \
  --expected-capsule-sha256 39392336669eaae56d34a7350eda82e85cc5ce41ae4ea2dbcb5a8195239b917b \
  --install
```

The install command uses a safe 64-byte echo vector with no `0xf4` trigger.
The separate opt-in probe confirms module identity, sends the trigger,
observes that the resident still responds over FMGO, then attempts a complete
Blink B upload. It requires the resident's `T` response and the same boot ID:

```sh
PICOTOOL=build/firmware/nano_rp2040_connect/application/usb-toolchain/data/packages/rp2040/tools/pqt-picotool/5.0.0-9576866/picotool
.venv/bin/python experiments/managed_sketch/probe_stall.py \
  --address 192.168.77.1 --device-id YOUR_16_HEX_DIGIT_ID \
  --expected-resident-uf2-sha256 3986a88b7fbe57fad389304dcd66ade38b59a7f015e3947472d3bafb71af5eb7 \
  --expected-stall-capsule-sha256 39392336669eaae56d34a7350eda82e85cc5ce41ae4ea2dbcb5a8195239b917b \
  --expected-blink-b-capsule-sha256 f9824db71be8536d952f9e4ef95476303aab76c68afa36b1cbc02b8405ed4659 \
  --expected-recovery-uf2-sha256 66b3cc527ffc175dc086e25adf6ed99c1bbae2d4be7036cb110701075294eb05 \
  --picotool "$PICOTOOL" \
  --run
```

After the trigger, disconnect D2 and return the board to REC/GND ROM mode.
Before repair, save `0x10200000..0x10201000` with pinned `picotool` and
compare its full-sector SHA-256 with
`975a53cb19fc41c0916b4b73d989d7668c409a407572b872b3f4a13f57e8dcc2`.
That hash is the exact stall capsule followed by erased `0xff` bytes. A
match would independently show that the timed-out network upload did not
change the sector. Then use the verified Blink B `flash_initial.py --picotool`
command above and rerun Blink B smoke. A mismatch or failed write requires
inspection before reboot. This check says nothing about stalls that disable
interrupts, corrupt shared memory, or interfere with USB hardware.

One selected Nano run returned `T` after 1.027 s and kept the resident's
FMGO hello available on the same boot. ROM readback matched the exact 4 KiB
stall sector, and the verified Blink B composite restored full exact-byte
service on a new boot. See the
[m7exp3 evidence](../../docs/evidence/managed-sketch-m7exp3.md).
