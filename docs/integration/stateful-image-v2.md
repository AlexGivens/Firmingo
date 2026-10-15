# Candidate stateful managed image — FMS2

Status: portable candidate validator and RAM-generation initialization proof,
2026-10-15, followed by an [opt-in resident allocation and ARM link proof](stateful-link-proof.md).
No current deployment resident advertises or accepts this format. FMS1/ABI one,
existing exports, compiler recipes/catalogue and SDK deployment pins remain
unchanged. This is the first implementation step of
[V1-STATE](production-v1.md), not stateful sketch support on a board.

## Ownership and limits

Firmware supplies a trusted `Contract`: exact resident API address, allocated
state-window address/capacity, and maximum capsule bytes. Image fields must match
these facts. The portable proof selects **no hardware RAM address or capacity**.
Its native fixture's `0x20030000`/32-byte window is a synthetic host mapping and
must never be offered as a device descriptor.

This candidate uses the existing Nano slot `0x10200000`, payload/code offset 256,
and a maximum capsule limit no greater than 4096 bytes. These are candidate proof
limits, not an increase in the current upload target. The RAM contract requires
a nonempty word-aligned window wholly in RP2040 main SRAM
`[0x20000000, 0x20040000)`, excluding both scratch banks. Containment alone does
not reserve memory or prove separation from resident globals, heap, stacks or
queues. The separate allocation proof now reserves a real symbol, checks its
actual address/size and records its budget. Runtime integration and a deployable
versioned export remain prerequisites for compiler adoption.

Image version 2 below is distinct from the FMGO protocol and product version.
The separate allocation proof defines a candidate FAPI-v2 table; the portable
fixture below still uses synthetic addresses. Neither proof supplies a usable
resident deployment descriptor.

## Exact bytes

All integers are unsigned little-endian. The API address is word-aligned resident
flash; entry addresses are Thumb pointers with at least two instruction bytes in
the code extent. `.data` follows code in flash and starts at the resident state
base in RAM; `.bss` immediately follows `.data` in RAM. Section sizes are rounded
to four bytes by the future linker. Empty data and BSS sections are permitted.

| Offset | Bytes | Field and required meaning |
| ---: | ---: | --- |
| 0 | 4 | ASCII `FMS2` |
| 4 | 2 | Image version: 2 |
| 6 | 2 | Header size: 128 |
| 8 | 4 | Nano tag: `0x4f4e414e` |
| 12 | 4 | Exact trusted resident API address |
| 16 | 4 | Code address: `0x10200100` |
| 20 | 4 | Nonzero code bytes, multiple of four |
| 24 | 4 | Setup Thumb entry within code |
| 28 | 4 | Loop Thumb entry within code |
| 32 | 4 | Data RAM address: exact resident-owned state base |
| 36 | 4 | Initialized data bytes, multiple of four |
| 40 | 4 | BSS RAM address: state base + data bytes |
| 44 | 4 | BSS bytes, multiple of four; data + BSS must fit the trusted capacity |
| 48 | 4 | Data load offset in capsule: 256 + code bytes |
| 52 | 4 | Exact capsule bytes: data load offset + data bytes |
| 56 | 4 | Flags: zero |
| 60 | 4 | Reserved: zero |
| 64 | 32 | SHA-256 of payload `[256, capsule bytes)` |
| 96 | 32 | SHA-256 of header `[0, 96)`, including the payload hash |
| 128 | 128 | Zero padding |
| 256 | code bytes | Code/readonly bytes |
| 256 + code bytes | data bytes | Initial RAM contents |

`validate_image` accepts an **exact capsule span**. Truncation, trailing bytes,
overflow, wrong identities/addresses, unsupported versions, malformed extents,
nonzero reserved bytes and either hash mismatch fail without a usable result.
It compares bounds before hashing or forming payload pointers. A future boot
adapter must produce a bounded exact view from the slot/header and installation
record; passing the whole erased slot is not this API's contract.

Header and payload hashes provide integrity, not publisher authentication,
authorization, installed-image attestation or machine-code safety. The existing
physical arm and independently validated deployment/source contracts still apply.

## Generation lifecycle

`Generation` owns a caller-supplied writable `Arena` and is noncopyable. One runtime
owner must call it only after the old sketch core is quiescent. The helper has
no USB, flash, scheduler, peripheral, callback or entry-execution side effects.

1. `begin(image, size, contract, token)` rejects an active generation. A nonzero,
   strictly increasing 32-bit token prevents accidental reuse/reset; exhaustion
   requires a new owner after reboot rather than wrapping silently.
2. It validates the complete image again, then checks the actual arena pointer,
   exact byte capacity and declared mapping against the trusted contract. Image
   storage overlapping the destination is rejected before mutation.
3. It clears the **entire arena**, copies the bounded initialized-data bytes,
   then publishes the accepted token and active state. Unused tail bytes are
   cleared so a smaller replacement cannot inherit a preceding sketch's RAM.
4. The future runtime may call setup only after success, then loop repeatedly
   without invoking initialization again. Mutable globals persist between those
   calls. No constructors, heap or nonvolatile persistence are introduced.
5. `stop()` revokes activity without clearing RAM or performing core parking.
   A failed subsequent begin leaves RAM and the last accepted token unchanged;
   entry execution must remain disabled until a new begin succeeds.

This RP2040 design is not a sandbox for arbitrary native sketch code. The window
limits initializer writes; they cannot prevent owner-authorized code from
accessing other memory. Caller-owned storage and single-owner/quiescence
preconditions remain necessary.

## Implemented checks and next work

The ordinary sanitizer/native entry point includes ten behavior cases using an
independent Python `struct`/`hashlib` fixture. They cover all truncated lengths,
metadata corruption with recomputed hashes, bounded entries and RAM extents,
data/BSS extremes, replacement cleanup, generation reuse/wrap, aliasing and guards.
Host writes model sketch state; the synthetic Thumb bytes are never executed.

`FIRMINGO_STATEFUL_FUZZ=ON` adds a separate fixed-seed 20,000-case mutation job
checking both accepted initialization and rejection without writes. CI includes
it alongside existing mutation jobs. The managed build job separately compiles
`stateful_image.cpp` for pinned Cortex-M0+ GCC with C++11 and warnings as errors;
that object is not linked into the old resident.

The [allocation/link proof](stateful-link-proof.md) now supplies real resident RAM,
candidate API/linker facts and a separately linked ARM sketch with actual
`.data`/`.bss`, checked through this production initializer on the host. Next:
integrate the runtime lifecycle and export the versioned deployment contract
before Compiler packaging/recipe validation and FirmingoKit adoption. Physical startup,
replacement and interruption qualification remain separate selected workflows.

[Local evidence](../evidence/stateful-image-v2.md) records actual tests, mutations,
source identities and ARM object; remote CI is reviewed separately.
