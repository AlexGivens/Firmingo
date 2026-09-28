# M7 Nano `m7exp3`: slot repair, network replacement, bounded pause

2026-09-25, macOS, operator-selected Nano RP2040 Connect. The user mounted
`RPI-RP2` with REC/GND; `INFO_UF2.TXT` identified the generic RP2040 ROM
bootloader. This does not independently attest the board model. D2 was left
disconnected. Board revision, cable, and hub were not recorded.

The resident now uses the pinned Arduino-Pico Nano variant's `D2` constant,
which maps to GPIO25, for the private upload arm input. It also reports a
distinct `0.1.0-m7exp3` version. The Arduino-Pico 6.0.0/CLI 1.5.1 compile
reported 131,752 flash and 93,472 static RAM bytes. The resident UF2 SHA-256
is `3986a88b7fbe57fad389304dcd66ade38b59a7f015e3947472d3bafb71af5eb7`;
the Blink A capsule SHA-256 is
`68516001acf84205a9af84e497aae598ba3be1c192188da5c1fc32ca361d609d`.
The exact-hash ROM-only composite was 302,080 bytes, SHA-256
`c2e48dafe8555c1ef1f69b3c177c97de3078ce21657c057f9804c0dfaeb40a40`.
`flash_initial.py` regenerated and verified the composite, then copied it to
the ROM volume; the volume ejected. This records a host copy, not flash
readback.

The USB network interface `en5` became active at `192.168.77.16/24`. FMGO
hello at `192.168.77.1` reported board ID `nano_rp2040_connect`, version
`0.1.0-m7exp3`, the same observed device ID as the previous Nano sessions,
and a new boot ID. Exact IDs are redacted here under the evidence directory's
public-transcript policy.

The nondestructive `smoke.py` check failed on its first 16,384-byte binary
echo: 16,544 framed wire bytes sent and **zero** echoed bytes received before
timeout. This repeats the `m7exp2` failure. A separate three-byte probe
reported `backend_rx_bytes:3`, `backend_tx_bytes:0`, no pending backend input,
and zero application TX peak. FMGO remained responsive. These counters show
that the resident accepts stream input; they do not establish that Blink A
validated or executed. The user observed D13 **off**. No network replacement
has been attempted.

The generated composite's sketch-slot UF2 payload matches the capsule
exactly, and the same production `validate_image` function accepts the
generated capsule on a native host.

The user returned the Nano to ROM mode. The bundled `picotool` identified one
RP2040 B1 device with 16 MiB flash and a flash ID matching the previously
observed FMGO device ID. A read-only `picotool save -r 0x10200000 0x10201000`
captured the first 4 KiB sketch-slot sector. Its SHA-256 was
`79bd7851b40c3b6b7b97bddcca6a9c2b25a35211d59c6e212a1fc88b9d0f3247`.
The first mismatch against the expected Blink A capsule was at byte 12: the
slot header recorded resident API address `0x10020104`, while the current
capsule and resident require `0x10022394`. The readback's bytes 32–63 also
are not the code SHA-256 required by the current image format. The rest of
the 4 KiB sector after the old 688-byte capsule was erased (`0xff`). This
is direct evidence that the `m7exp3` ROM UF2 copy did **not** replace the
sketch-slot sector with its included capsule. The current resident would
reject the old slot as `wrong_api`, explaining the off LED and zero echo.
The reason the composite copy skipped or failed to retain the slot blocks
remains unknown. No write was made during this readback.

Read-only `picotool verify` of the full `m7exp3` composite independently
reported the resident flash range **OK**, then the first mismatch at
`0x1020000c` in the sketch slot. Thus the failure is localized to the slot;
the composite's resident region matches the board byte for byte.

## Exact slot repair and Blink A retest

The user then authorized writing the single 4 KiB sketch-slot sector while
the same Nano remained in ROM mode. A sector image containing the exact
688-byte Blink A capsule followed by erased `0xff` bytes had SHA-256
`3ae4ca451bc5255ddde3ea09d877fc52d54951d8744af9b90b84c0d8cdd25cd2`.
`picotool load -v` wrote that image at `0x10200000` and reported OK.
Independent `picotool save` readback matched all 4,096 bytes and the same
SHA-256. The production image validator accepted the readback. Full
`picotool verify` of the resident-plus-Blink A composite then reported OK
for both flash ranges. `picotool reboot` started the application.

The board-specific commands were, from the repository root, with the selected
Nano already in ROM mode:

```sh
PICOTOOL=build/firmware/nano_rp2040_connect/application/usb-toolchain/data/packages/rp2040/tools/pqt-picotool/5.0.0-9576866/picotool
"$PICOTOOL" load -v build/managed_sketch/readback/m7exp3_blink_a_slot_4k.bin -t bin -o 0x10200000
"$PICOTOOL" save -r 0x10200000 0x10201000 build/managed_sketch/readback/m7exp3_slot_after_4k.bin -t bin
"$PICOTOOL" verify build/managed_sketch/composite/blink_a-initial-rom-only.uf2 -t uf2
"$PICOTOOL" reboot
```

The post-repair FMGO hello reported the same device ID, the `m7exp3`
version, and a new boot ID. On that boot, `smoke.py` passed:

| Check | Observed |
| --- | --- |
| Fragmented binary echo | 16,384/16,384 exact bytes in 0.634 s |
| Delayed reader | 4,096/4,096 exact bytes in 0.433 s |
| Second client while owned | Explicit busy response |
| Ownership transfer | 4,096/4,096 exact bytes |
| Fresh TCP session | 4,096/4,096 exact bytes; same boot ID |

The snapshot during that run showed 168,220 free C-heap bytes, 3,920 free
core-0 stack bytes, zero application RX/TX discards, and zero TCP errors.
Those samples do not cover core-1 stack or worst-case memory use. The user's
post-repair D13 observation confirmed the expected roughly 250 ms half-period.

## Physically armed network replacement

With USB still connected, the user bridged physical Nano D2 (GPIO25) to GND.
The exact-hash `upload.py --install` first sent the same Blink A capsule through
the private listener. The resident returned its installed/readback-success
response; the host verified the same boot ID and 4,096 exact echo bytes in
0.516 s. It then sent the separately linked Blink B capsule (692 bytes,
SHA-256 `f9824db71be8536d952f9e4ef95476303aab76c68afa36b1cbc02b8405ed4659`).
That upload also returned installed/readback success, kept the same boot ID,
and passed 4,096 exact echo bytes in 0.511 s. Both commands selected the
prior observed Nano device ID and the exact `m7exp3` resident UF2 hash.

After the A-to-B replacement, the full non-destructive `smoke.py` suite passed
on the same boot:

| Check | Observed |
| --- | --- |
| Fragmented binary echo | 16,384/16,384 exact bytes in 0.636 s |
| Delayed reader | 4,096/4,096 exact bytes in 0.430 s |
| Second client while owned | Explicit busy response |
| Ownership transfer | 4,096/4,096 exact bytes |
| Fresh TCP session | 4,096/4,096 exact bytes; same boot ID |

The snapshot showed 168,220 free C-heap bytes, 3,920 free core-0 stack
bytes, zero application RX/TX discards, and zero TCP errors. The user then
checked Blink B's D13 rate and one normal reconnect as recorded below. The
network installer has one successful A reinstall and one successful A-to-B
switch on this selected Nano; stalled-sketch, power-cut, repeated cold
attachment, and iPhone/iPad checks remain open.

## D2 removal and normal reconnect

The user removed the D2-to-GND jumper and observed the expected roughly
500 ms D13 on/off half-period for Blink B. After unplugging for five seconds
and reconnecting normally, without REC/GND, they observed the same blink.
The ROM volume was absent; macOS `en5` was active at `192.168.77.16/24`.
FMGO hello reported the same device ID and resident version with a **new**
boot ID. The full Blink B smoke passed again on that new boot: 16,384/16,384
fragmented echo bytes in 0.632 s, 4,096/4,096 delayed-reader bytes in
0.434 s, an explicit busy response for a second owner, 4,096/4,096 ownership
transfer bytes, and 4,096/4,096 fresh-session reconnect bytes. The snapshot
showed zero application RX/TX discards and zero TCP errors.

With D2 disconnected, a diagnostic client sent only a 60-byte valid upload
header, with no capsule and no commit marker. The listener responded `D`
(physical arm absent), and the board retained the same boot ID. This is one
denial observation; it does not prove all authorization edge cases. The
combined evidence supports one persistent A-to-B network replacement on one
selected Nano. Repeated cold cycles, stalled-sketch failure handling,
power-cut recovery, and iPhone/iPad checks remain open.

## Bounded five-second sketch pause

With the same Nano still on the normal `m7exp3` boot, the user again placed a
jumper from physical D2/GPIO25 to GND. A read-only FMGO hello confirmed the
same device ID, resident version, and boot ID as after the preceding normal
unplug/replug. `upload.py --install` installed the exact 696-byte
`pause_probe` capsule (SHA-256
`5641ec39766c29b5c29f44b26492285a3e7e7c5eee012b7f368e1c28a9d67887`).
The listener returned installed/readback success; a 64-byte exact echo check
passed, and the boot ID did not change. The diagnostic sketch reserves
`0xf0` to reply `0xf1` and enter `delay(5000)`; it uses a 375 ms D13
half-period. The LED rate was not visually checked in this step.

The opt-in `probe_pause.py --run` sent the pause trigger over FMGO, closed
that stream, and sent the complete exact Blink B capsule to the private
upload listener while the sketch was in the delay. The resident returned
`T` (park deadline timeout) after **1.019 seconds**, less than the five-second
delay. After that delay, a new FMGO session received the diagnostic sketch's
`0xf3` reply to `0xf2`, passed 64/64 exact echo bytes, and reported the same
boot ID. The native `SketchSwitch` timeout branch aborts the staged capsule
before calling the installer or stopping the old sketch. This hardware
observation exercises that refusal path, but no ROM readback of the slot was
performed during this step.

While D2 remained grounded, `upload.py --install` restored the exact Blink B
capsule (SHA-256
`f9824db71be8536d952f9e4ef95476303aab76c68afa36b1cbc02b8405ed4659`).
It returned installed/readback success, kept the same boot ID, and passed
4,096/4,096 exact post-install echo bytes. Full Blink B smoke then passed:
16,384/16,384 fragmented binary echo in 0.632 s; 4,096/4,096 delayed-reader
echo in 0.435 s; explicit busy response for a second owner; 4,096/4,096
ownership-transfer echo; and 4,096/4,096 fresh-session echo. The snapshot
showed zero application RX/TX discards and zero TCP errors. The user then
removed D2-to-GND and confirmed D13 still blinked appropriately for Blink B.
This was a visual observation, not an instrumented timing measurement. A
read-only FMGO hello after jumper removal reported the same device ID,
resident version, and boot ID.

This proves one **bounded** delay timeout and continued service on one board.
It does not cover a permanently stuck `loop()`, a sketch blocking with
interrupts disabled, power loss during flash, repeated cold connections, or
iPhone/iPad attachment.

## Full-composite ROM install with pinned picotool

After the bounded-pause check and Blink B restoration, the user removed D2
and confirmed D13 still blinking appropriately. A read-only FMGO hello kept
the same boot ID. The user then deliberately returned the selected Nano to
REC/GND ROM mode; `RPI-RP2` appeared. D2 remained disconnected.

The pinned `picotool info -a` identified one RP2040 B1 with 16 MiB flash and
the flash ID matching this Nano's prior board record. The host regenerated
the exact Blink B composite from the resident and capsule reports: 302,080
UF2 bytes, 587 resident blocks and three sketch blocks, SHA-256
`66b3cc527ffc175dc086e25adf6ed99c1bbae2d4be7036cb110701075294eb05`.
`flash_initial.py --picotool` checked that flash ID before writing, called
`picotool load -v`, then independently called `picotool verify` for the full
composite. Both reported **OK** for the resident range and the sketch range.
Only then did the tool reboot into application mode. This is a readback
verification of the UF2-covered bytes; it does not attest to untouched flash
outside those ranges.

FMGO hello after reboot reported the same device ID and resident version
with a new boot ID. Full Blink B smoke passed on that boot: 16,384/16,384
fragmented binary echo in 0.630 s, 4,096/4,096 delayed-reader echo in
0.438 s, explicit second-owner busy response, 4,096/4,096 ownership-transfer
echo, and 4,096/4,096 fresh-session echo. The snapshot had zero application
RX/TX discards and zero TCP errors. The Blink B variant is attested by the
verified composite and byte behavior. The user then visually confirmed D13
blinking on and off at roughly 500 ms intervals on this new boot. That is an
operator timing estimate, not an instrumented measurement.

This qualifies one exact-hash `picotool` full-composite initial install on
the selected recoverable Nano. It does not explain why the earlier
mass-storage UF2 copy skipped the sketch slot, nor does it qualify another
board, core version, power-cut path, or iPhone/iPad attachment.

## Deliberately nonreturning sketch

The user subsequently bridged D2/GPIO25 to GND for an opt-in permanent-stall
check. The Nano's read-only FMGO hello still matched its prior device ID,
`m7exp3` version, and post-ROM-install boot ID. `upload.py --install`
installed the exact 696-byte `stall_probe` capsule (SHA-256
`39392336669eaae56d34a7350eda82e85cc5ce41ae4ea2dbcb5a8195239b917b`),
returned installed/readback success, passed a safe 64-byte exact echo, and
kept the same boot ID.

The separate `probe_stall.py --run` first checked the exact Blink B ROM
recovery image and pinned `picotool`. It confirmed the diagnostic sketch's
`0xf6`→`0xf7` identity response, then sent `0xf4`. The sketch replied
`0xf5` and entered a disassembled `nop`/backward-branch loop on core 1.
The resident answered FMGO hello while the sketch was stuck. A complete
Blink B network upload returned `T` after **1.027 seconds**; FMGO hello
still reported the same boot ID afterward. The production timeout path
aborts staging before calling the flash installer. At this point the
no-write conclusion rested on that code path and the `T` response; the
ROM readback below independently checks the sector.

The running sketch could not reach the resident's park point and could not
be replaced over the network. The user removed D2 and entered REC/GND ROM
mode. Pinned `picotool info -a` again reported the matching RP2040 B1,
16 MiB flash ID. A read-only `picotool save -r 0x10200000 0x10201000`
captured the full first sketch-slot sector. All 4,096 bytes matched the
exact stall capsule followed by erased `0xff` bytes; both expected and
observed sector SHA-256 were
`975a53cb19fc41c0916b4b73d989d7668c409a407572b872b3f4a13f57e8dcc2`.
This independently confirms that the timed-out Blink B upload did **not**
change that sector. It does not attest to untouched flash elsewhere.

The exact Blink B composite (SHA-256
`66b3cc527ffc175dc086e25adf6ed99c1bbae2d4be7036cb110701075294eb05`)
was then loaded via `flash_initial.py --picotool`. Its load-time verification
and separate full-UF2 verification both reported **OK** for resident and
sketch ranges before reboot. FMGO hello after reboot had the same device ID
and resident version with a new boot ID. Full Blink B smoke passed:
16,384/16,384 fragmented echo bytes in 0.628 s; 4,096/4,096 delayed-reader
bytes in 0.439 s; explicit second-owner busy response; 4,096/4,096
ownership-transfer bytes; and 4,096/4,096 fresh-session bytes. The snapshot
showed zero application RX/TX discards and zero TCP errors. D13 timing after
this recovery was visually confirmed by the user at roughly 500 ms on and
500 ms off. This is an operator estimate, not an instrumented measurement.

With Blink B running after recovery, a FMGO `device.reset` request with
`scope:"device"` returned the explicit `unsupported` error. A fresh FMGO
hello kept the same boot ID. This confirms rejection on this image; it is
not a sketch-only reset mechanism or a test of the physical RESET input.

This one test establishes that this specific nonreturning core-1 sketch
leaves the resident network service reachable, refuses network replacement
without a sketch-slot write, and can be recovered through the Nano's ROM
path. It does not establish survival of stalls that disable interrupts,
alter shared RAM, interfere with USB hardware, or corrupt the resident.
