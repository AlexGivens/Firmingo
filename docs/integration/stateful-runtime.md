# Candidate FMS2 boot and replacement lifecycle

Status: portable lifecycle and opt-in Nano boot adapter implemented; native and
compile/link evidence is [recorded separately](../evidence/stateful-runtime.md).
No hardware execution is claimed. The later [target-2 adapter](stateful-upload-contract.md)
adds a separately selected upload build; this document describes the boot-only mode. This follows the
[allocation/link proof](stateful-link-proof.md) and advances
[V1-STATE](production-v1.md). Compiler recipes, SDK pins and installed firmware
remain on the earlier experimental contract.

## Ownership and ordering

`stateful::Runtime` owns one `Generation`, its trusted contract, entry metadata,
and an execution port. Core 0 owns lifecycle operations; core 1 alone calls
`tick()` at its cooperative boundary. The board must keep the existing production
`Core1Pause::park_if_requested()` before every tick. Arena and port outlive the
runtime; native code retains its documented unsandboxed ownership model.

Boot is attempted once per runtime instance. `boot_from_slot` reads the length
field only after a mapped 256-byte prefix exists, bounds it to the mapped bytes,
trusted maximum and 4096-byte proof limit, then passes that **exact view** to
production FMS2 validation/initialization. It never hashes the whole 4 MiB slot
or treats an untrusted length as a pointer extent. Failed boot leaves RAM and
entry publication unchanged; a second boot call cannot reset a live generation.

Initialization clears the full owned arena and copies data before writing entry
metadata and release-publishing a nonzero, increasing token. Core 1 acquires it,
calls setup once for that token, and calls loop on subsequent ticks without
reinitializing RAM. A returned first loop is recorded separately. That flag is
an observed return, not an application-health or image-attestation guarantee.
Token exhaustion refuses another start; it never silently wraps.

The execution port invokes target Thumb entries on the board. Native tests and
the artifact checker supply observation ports that model setup/loop memory
writes; they never execute synthetic or generated ARM code.

## Replacement driver

`stateful::Replacement` is a firmware driver with a private 4 KiB RAM snapshot,
not a network service. A future adapter still needs owner/session IDs, transaction
status, authorization checks and the versioned target contract.

1. `request` requires an attempted boot, physical-arm approval, an exact valid
   capsule, available token space and an idle driver. It captures and revalidates
   caller-owned immutable input, then requests the existing core-1 park.
2. `poll` runs only on core 0 outside USB/lwIP callbacks and locks. Removing the
   arm or exceeding the 1000 ms pause deadline aborts without stopping the old
   generation or touching flash. Time subtraction handles clock wrap.
3. Once parked and still armed, it revalidates the snapshot, revokes the active
   token and loop-return flag, and calls the port's stop/discard hook **before**
   opening flash. No later failure resumes old entries.
4. It erases only the fixed first managed sector and programs 256-byte RAM pages
   padded with `0xff`. The existing flash port owns interrupt masking. All
   initialized capsule bytes and the erased tail must match readback.
5. It derives an exact view from the mapped readback and initializes the next
   generation before publication. Only then does it release core 1. Every failure
   also releases core 1, with execution disabled after the stop boundary.

`ReplacementReport` separates flash result, image/initialization error, overall
status and whether the slot may have been touched. A good readback alone is not
successful startup: wrong arena wiring can still fail initialization. A later
explicit request can recover from an invalid boot or a partial write; idle polls
never replay a replacement. On reboot the actual slot is validated afresh; there
is no persistent failed-transaction latch, atomic multi-slot update or rollback.

## Opt-in board build

`build_resident.py --stateful-runtime-proof` implies the existing allocation
flag and links the same portable runtime used by native tests. It uses the
resident-owned array and exact candidate API address, a distinct
`-statefulboot-exp1` firmware identity, and no upload-service adapter. The candidate
exposes neither the old FMS1 upload target nor a new FMS2 target. The replacement
driver is native-tested but is not wired to a board network command.

The readonly ELF symbol `firmingo_stateful_boot_mode=1` distinguishes this build
from the allocation-only proof. The proof tool verifies its actual section and
LOAD bytes, the API, arena, source hashes and initialized ELF/UF2 agreement. Its
JSON remains a build receipt, not a deployable descriptor. The old exporter
continues to reject both candidate report statuses.

After preparing the pinned overlay and building the native checker as described
in [the link proof](stateful-link-proof.md), select a new output directory:

```sh
python experiments/managed_sketch/build_stateful_proof.py --boot-runtime \
  --cli "$(command -v arduino-cli)" \
  --config-file build/firmware/nano_rp2040_connect/managed/usb-toolchain/arduino-cli.yaml \
  --validator build/stateful-check/managed_stateful_check \
  --output-dir build/stateful-runtime-proof
```

The default proof command still selects allocation only. CI builds the ordinary
resident, allocation-only proof and boot-runtime proof as separate observable
steps, plus standalone Cortex-M0+ C++11 compilation with warnings as errors.
Ordinary sanitizer tests include the production replacement driver with fake
flash and the real cooperative pause. No operation here flashes a board.

## Next integration

The [versioned export and upload adapter](stateful-upload-contract.md) now provide
firmware resources/build facts and an opt-in target-2 service with protocol
fixtures and retained status. Compiler packaging/recipes and SDK
deployment checks must adopt those exact facts together. Qualify actual setup,
loop state, replacement, interruption and recovery on a selected Nano; native
fault injection and linker headroom cannot establish board behavior or measured
heap/stack stability.
