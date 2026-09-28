# Nano managed-sketch layout decision for the M7 proof

This decision applies to the experimental Arduino Nano RP2040 Connect
`m7exp3` path built with Arduino-Pico 6.0.0 and the board's 16 MiB
`flash=16777216_0` selection. It does not change the 0.1.0 release images,
define a public capsule format, or qualify a four-megabyte upload. The
[hardware record](../evidence/managed-sketch-m7exp3.md) identifies the exact
resident and capsules used so far.

## Ownership and flash ranges

Addresses are RP2040 XIP addresses; end addresses are exclusive. All
boundaries used for erase and program operations are 4 KiB aligned.

| Owner | Start | End | Size | M7 use |
| --- | --- | --- | ---: | --- |
| ROM boot entry and Firmingo resident | `0x10000000` | `0x10200000` | 2 MiB | Arduino-Pico startup, NCM, DHCP, FMGO, module loader |
| Managed sketch slot | `0x10200000` | `0x10600000` | 4 MiB | First 4 KiB only in this proof |
| Raw incoming-image stage | `0x10600000` | `0x10a00000` | 4 MiB | Reserved, never written by this proof |
| Install metadata | `0x10a00000` | `0x10a10000` | 64 KiB | Reserved, never written by this proof |
| Unassigned | `0x10a10000` | `0x10fff000` | 6,221,824 bytes | No writes |
| Existing EEPROM reservation | `0x10fff000` | `0x11000000` | 4 KiB | Preserved board convention; no proof writes |

The resident is always the boot image. On each boot it starts local-only NCM,
DHCP, the FMGO stream, and the private upload listener before making USB
visible. It checks the slot header, board tag, exact resident API address,
entry points, limits, padding, and code digest before starting a sketch on
core 1. An invalid slot leaves the resident and diagnostic path available.
The slot is separately linked with code at `0x10200100`; a conventional
whole-board Arduino UF2 is not a managed sketch.

The current installer accepts at most a 4 KiB capsule in RAM, requires
physical D2-to-GND arming, waits up to one second for core 1 to park in
SRAM, then erases and programs only `0x10200000..0x10201000`. It reads the
sector back before reporting success. The resident forbids a live FMGO
owner at commit and keeps USB/lwIP work on core 0. During flash operations,
core 0 masks its interrupts and core 1 must remain parked in SRAM; no
network callback performs a flash write. These are proof invariants, not
evidence that a long or permanently stuck sketch can be replaced.

## RAM and update choice

The pinned RP2040 linker exposes 256 KiB of main SRAM and two 4 KiB scratch
banks. The historical `m7exp3` resident compile reports 93,472 static RAM bytes and
131,752 flash bytes. The proof module has no writable `.data` or `.bss`,
and the 4 KiB capsule stage is part of the resident's static allocation.
The M9 resident builder now enforces the existing 192 KiB static-RAM and
1 MiB flash engineering budgets, and the module builder limits capsules to
the installer's 4 KiB RAM stage. These caps do not mean the remaining RAM is freely
available to arbitrary sketches. One hardware snapshot reported 168,220 free C-heap
bytes and 3,920 free core-0 stack bytes. It did not measure core-1 stack,
lwIP pool exhaustion, or a worst-case concurrent upload. Mutable sketch
state and larger capsules stay outside the compatibility claim until joint
RAM and stack limits are measured and enforced.

For managed-sketch replacement, use a resident-owned raw stage and metadata
protocol when images grow beyond one RAM-held sector. The pinned
Arduino-Pico `Updater` writes an incoming firmware file to LittleFS and its
OTA boot flow installs a whole main sketch after reboot. This board selection
has no LittleFS partition, and whole-sketch replacement would overwrite the
resident/service image. `Updater` is therefore not the managed-sketch
installer for this layout. A future resident update would be a separate,
explicitly recoverable operation; only the RP2040 ROM path is used for initial
installation and recovery in M7.

The single active slot is not an atomic A/B design. A power cut during an
in-place slot erase/program may leave no runnable module, although the
resident should still boot. The reserved stage and metadata do not yet
provide resume, rollback, or validity records. Until those behaviors are
implemented and tested, an uncertain install requires ROM-mode inspection
and possibly repair. The earlier mass-storage UF2 copy left an old slot in
place even though the resident changed, so a full-composite initial install
must be verified by reading or verifying **both** flash ranges before normal
reboot.

This decision is Nano-specific. The Pico's 2 MiB flash layout needs a
separate design. Before claiming the full ranges above, add linker and write
guards for resident/slot/stage/metadata/EEPROM, a bounded staging protocol,
power-interruption recovery, and measured joint memory limits.
