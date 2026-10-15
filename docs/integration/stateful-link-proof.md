# Nano candidate state allocation and ARM link proof

Status: compile/link and native host initialization passed on 2026-10-15.
This advances [V1-STATE](production-v1.md) after the portable
[FMS2 validator](stateful-image-v2.md). It does not enable FMS2 boot, upload,
execution or compiler/SDK deployment. No board operation is part of this proof.

## Resident allocation and API

`build_resident.py --stateful-allocation-proof` selects an explicit candidate
build of the existing resident. Default builds retain FAPI one/FMS1. The candidate
adds `firmingo_sketch_state`, a word-aligned **4096-byte `.bss` array**, and roots
`firmingo_sketch_api_v2` through the linker. Its pointer retains that allocation;
this is owned static RAM, not an assumed free region above the heap start.

The candidate 52-byte `FirmingoSketchApiV2` contains:

| Offset | Bytes | Meaning |
| ---: | ---: | --- |
| 0 | 4 | FAPI magic `0x49504146` |
| 4 | 2 | ABI version 2 |
| 6 | 2 | Table size 52 |
| 8 | 32 | Same eight function pointer offsets/signatures as ABI one |
| 40 | 4 | Resident-owned state address |
| 44 | 4 | State capacity: 4096 |
| 48 | 4 | Maximum candidate capsule bytes: 4096 |

The old API table remains separate for the old runtime. The proof facade uses
its existing call offsets against the candidate table's first member; this does
not make the version-2 table an ABI-one identity. A future versioned compiler
contract must export the appropriate types, facade, linker constraints and facts.
The allocation candidate's report has a distinct status, `runtimeEnabled: false`
in its derived facts, and no normal `upload_target`. The existing exporter
rejects it. `linker-contract.json` is a local proof receipt, not a deployable
resident descriptor or compiler recipe.

`build_stateful_proof.py` reads actual ELF section, symbol and LOAD bytes. It
checks both tables' function pointers, the candidate table's readonly flash
mapping, the arena's exact writable NOBITS symbol, other object ownership and
linker BSS/heap/stack boundaries. It compares every initialized resident ELF byte
with the UF2 and records memory totals and hashes. A nominal heap span is a linker
extent; dynamic heap use and stack peaks still require measurement.

## Independently linked sketch

The selected `stateful_counter.ino` has one volatile initialized word (`7`) and
one volatile BSS counter. Setup tests their initial values; loop updates both
through real ARM RAM loads/stores and uses resident GPIO/delay calls. The proof
links this separately from the resident, without an SDK startup, constructors,
heap or runtime libraries.

`stateful_module.ld` receives the arena address/capacity derived from the ELF.
Code starts at `0x10200100`; initialized data has that RAM VMA and a contiguous
flash LMA after code. BSS follows data in the arena and has no file bytes. Explicit
program headers exclude ELF headers from the flash payload. Constructors,
unwinding and TLS are rejected by a retained-section assertion. The Python reader
also rejects unexpected allocated sections, remaining relocations, undefined
symbols, overflowing RAM/flash extents, mismatched LOADs and invalid entry points.
It constructs the candidate capsule independently with `struct` and `hashlib`.

The CMake `managed_stateful_check` executable then invokes the **production** C++
validator and `Generation` initializer on those actual linked bytes. Guarded host
RAM must receive exact initialized data, zero BSS and zero unused tail. An active
generation cannot reinitialize; explicit stop permits the next generation and
clears old state. A corrupted generated capsule must fail. Host mutations model
state retention; no ARM instruction is executed by this check.

## Reproduce

First build the ordinary profile to prepare the pinned overlay (see
[managed profile setup](../managed-profile.md)); configure `ARDUINO_CLI` and
`ARDUINO_CONFIG_FILE` for your installed pinned tools. Then:

```sh
python tools/dev.py build --board nano_rp2040_connect --firmware managed
cmake -S . -B build/stateful-check -DFIRMINGO_SANITIZE=ON
cmake --build build/stateful-check --target managed_stateful_check --parallel 2
python experiments/managed_sketch/build_stateful_proof.py \
  --cli "$(command -v arduino-cli)" \
  --config-file build/firmware/nano_rp2040_connect/managed/usb-toolchain/arduino-cli.yaml \
  --validator build/stateful-check/managed_stateful_check \
  --output-dir build/stateful-proof
```

Select the CLI's absolute path if it is not on PATH. The output directory must
be new: previous evidence is never overwritten. `report.json` records failure
before build work; success requires all validation and source rechecks to pass.
The command has `--help`, uses only this checkout and the pinned prepared overlay,
and never connects to or flashes hardware. Generated UF2s remain unqualified.
CI runs the same command separately from the old resident/export/capsule path and
retains configuration/build logs, ELF/UF2, map, objects, capsule and JSON receipts.
Host fixture tests run through `tools/dev.py test --sanitize` without ARM tools,
a board or a sibling checkout.

## Next gates

Integrate a separately selected FMS2 runtime with exact bounded boot views,
quiescent generation ownership, initialization before setup, and safe replacement
failure behavior. Export a versioned contract with actual build facts only after
that integration; then add Compiler packaging/recipe validation and SDK adoption.
Measure aggregate RAM/stack use and qualify startup, repeated loops, replacement,
interruption and recovery on an explicitly selected board. Existing consumer pins
and installed firmware remain the earlier qualified experimental path.

[Measured local evidence](../evidence/stateful-link-proof.md) records exact bytes,
commands, initial rejection and validation limits.
