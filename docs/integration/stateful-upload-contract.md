# Experimental FMS2 contract and upload adapter

Status: native/host validation and opt-in Nano compile/link implemented. Hardware
execution remains unqualified. This extends the [stateful runtime](stateful-runtime.md)
without changing the default ABI-one resident, its exporter or its fixtures.
[Evidence](../evidence/stateful-upload-contract.md) records actual results.

## Independent versions and ownership

| Interface | Version/identity |
| --- | --- |
| FMGO framing and upload methods | Major 1, existing request/response/type-4 schemas |
| Upload target | ID 2, `nano-managed-v2` |
| Resident calls/state table | FAPI ABI 2, 52 bytes |
| Capsule | FMS2, image version 2, 128-byte header, code offset 256 |
| Readonly upload facts | FUP2, table version 1, 56 bytes |
| Firmware resource contract | `firmingo-nano-managed-abi2-v1-experimental`, contract version 1 |
| Export manifest | Schema 3, `experimental_stateful_resident_compiler_export` |

Firmware supplies these constraints and actual resident facts. Compiler recipe,
catalogue, preprocessing and library identities remain Compiler-owned. FirmingoKit
owns device selection, association, authorization workflow and deployment checks.
No consumer pin is automatically migrated. This bundle is experimental input for
explicit consumer adoption, not a hardware-qualified production release.

## Upload behavior

The opt-in resident `-statefulupload-exp1` advertises only target 2. The FMS1 target
and format are rejected. The ordinary M9 build still advertises only target 1;
release references have no upload target. A representative request is:

```json
{"op":"flash.begin","target_id":2,"board_id":"nano_rp2040_connect","format":"nano-managed-v2","size":380,"sha256":"<64 lowercase hex characters for the selected capsule>"}
```

Other methods retain their existing schemas, selecting `target_id:2` and the
returned `upload_id`. Type-4 ID/offset fields remain **big-endian**; the embedded
FMS2 header remains little-endian. Maximum capsule/chunk sizes are 4096/1024 bytes.
The new [byte-level fixtures](../../tests/fixtures/protocol-v1/managed-upload-v2/README.md)
include exact request/response frames, a non-executable synthetic capsule and
explicit old-target rejection. The sample size above is one native proof, not a
fixed image size.

`stateful::Upload` implements the existing `UploadService` interface. One session
owns begin/chunk/finish/commit/abort; another session may read the retained status.
Chunks must be nonempty, sequential and bounded. Finish checks both the full
transfer SHA-256 and production FMS2 validation before reporting `verified`.
Begin and commit exclude an owned console through production Session; staging
blocks new console opens. Invalid requests do not take another owner's stage.

The D2/GPIO25-to-GND arm is checked at begin, commit, and the outer-loop poll before
requesting replacement; the replacement driver checks it again before flash.
Commit acknowledges **queued** work. Callbacks never park a core or write flash.
The resident calls `poll()` outside its lwIP lock to drive the tested replacement
lifecycle. Readback plus RAM initialization produces `installed`; only the exact
installed generation's returned first loop produces `boot_confirmed`. An older
generation's returned loop cannot satisfy that condition.

Accepted/verified transfers abort and scrub staging on owner disconnect. Queued
commits retain ownership/status and proceed through the outer loop; reconnect
only queries status and never retries commit automatically. IDs are never reused
within a boot. Only the most recent transaction is retained, including terminal
failure and `slot_touched`; a new begin replaces that record.

A 30,000 ms absolute staging lease starts at begin, using the port's last-polled
monotonic clock. It includes verification and a commit still waiting for its
first port poll; traffic does not extend it. Expiry fails with existing FMGO
`io_error`, leaves flash untouched and clears staging. This avoids introducing a
new shared wire error. Once replacement starts, its separate 1000 ms park deadline
applies. No timer automatically retries an operation.

The adapter has its own 4 KiB stage and the replacement driver has an independent
4 KiB immutable snapshot. The explicit memory cost protects ownership across the
commit boundary and is included in the new resident's static RAM budget. There
is no rollback or persistent failure latch; reboot validates actual slot bytes.

## Versioned export

`export_stateful.py` accepts only the upload candidate, with an exact committed
source inventory, pinned board/profile, memory budgets and matching ELF/UF2 hashes.
It checks actual FAPI calls/state allocation, readonly FUP2 bytes, boot marker,
firmware identity, linker RAM bounds and all initialized flash bytes against UF2.
The report's static RAM must agree with the actual linker end. Validators must
come from the selected commit. Allocation-only, boot-only and ABI-one reports
are rejected; the old exporter also continues to reject FMS2 candidates.

The output contains resident ELF/UF2, report, build facts, a checksummed manifest,
and a versioned language-neutral JSON contract with field offsets, hash spans,
API signatures/offsets, ARM constraints, GPIO restrictions, initialization and
upload semantics. Resources include the explicit ABI-two facade, entry/linker
references, production image-validator reference sources, GPL-3.0 license and
immutable protocol/parser fixtures. These are firmware interfaces/reference
inputs; this repository does not add another WASM compiler or compiler recipe.

Facts include exact API/RAM addresses, capacity, image/ELF/table/report/contract
hashes, target ID/limits and firmware commit. Numeric address agreement alone is
not installed-image attestation. Wrappers must retain the independently reviewed
resident UF2 association and recheck the device before deployment. Hashes provide
integrity, not authentication or an arbitrary-native-code sandbox.

The exporter validates everything before creating a **new** output directory.
It uses an explicit committed resource inventory, not the current working tree's
file discovery. No sibling checkout, board or network is needed.

## Reproduce

Prepare the pinned overlay through the ordinary managed build and build the
native checker as in [the runtime workflow](stateful-runtime.md). Then:

```sh
python experiments/managed_sketch/build_stateful_proof.py --upload-runtime \
  --cli "$(command -v arduino-cli)" \
  --config-file build/firmware/nano_rp2040_connect/managed/usb-toolchain/arduino-cli.yaml \
  --validator build/native-sanitize/managed_stateful_check \
  --output-dir build/stateful-upload
python experiments/managed_sketch/export_stateful.py \
  --resident-dir build/stateful-upload/resident \
  --firmware-commit "$(git rev-parse HEAD)" \
  --output build/stateful-export
```

Source must match the selected commit; both output directories must be new.
`--help` documents both commands. CI builds all maintained modes, exports this
bundle against its exact commit, and retains resources/binaries/logs. Ordinary
sanitizer tests include native Session/adapter/runtime cases and host export
rejection/determinism checks. `FIRMINGO_STATEFUL_UPLOAD_FUZZ=ON` enables a separate
fixed-seed 2000-case adapter mutation job alongside the five existing jobs.

Next: explicit Compiler schema/recipe/packaging adoption, independent capsule
validation and SDK contract/deployment migration; then selected Nano/iPad/iPhone
startup, replacement, interruption and recovery qualification. No hardware write
or release publication is performed by these build/export commands.
