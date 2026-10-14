# Firmware contract for Firmingo-Compiler and wrappers

Ownership accepted on 2026-10-12. This document specifies the firmware side of
the compiler split, including the experimental exact-build exporter added on
2026-10-13. It does not change the ABI/protocol or qualify device execution. The existing
[managed-sketch handoff](managed-sketch-sdk.md) remains the experimental M9 guide.

## Ownership and inputs

| Layer | Authoritative responsibility |
| --- | --- |
| Firmingo | Resident ABI/calling convention, facade behavior and pins, slot/memory limits, image format, protocol/capabilities, authorization, execution, install/recovery behavior, firmware build artifacts and provenance |
| Firmingo-Compiler | Supported source preprocessing, web/WASM execution, explicit compile/link recipes, ELF/capsule packaging, diagnostics, compiler asset inventory, static compatible build descriptors and receipts |
| FirmingoKit or another wrapper | Platform web hosting, resource access, device evidence/association, communication, installation, upload, deployment checks, and presentation-facing APIs |

Firmingo exports actual build facts. Firmingo-Compiler adds compatible recipe
identities rather than making firmware depend on a host compiler release.
Wrappers consume compiler packages and firmware installation artifacts through
their respective validation boundaries. No layer's ordinary tests/builds require
an adjacent checkout; imported contracts and packages are versioned inputs.

Keep native firmware builds, GCC proof modules, firmware validators, and hardware
harnesses here. The proof linker/facade defines the device constraints; compiler
recipes may adapt those inputs only with explicit compatibility checks. Do not
maintain competing authoritative layouts or relax firmware validation merely to
accept a compiler artifact.

## Parser export and experimental exact-build export

`experiments/managed_sketch/export_sdk_contract.py` currently exports documentation,
ABI/image headers, and protocol fixtures. Its manifest explicitly declares no
firmware images and non-executable fixtures. Preserve that contract and command;
the ZIP is not an exact resident-build export or executable test package.

The broader production export must eventually provide:

- Exact resident ELF and installation UF2, distinguishing resident-only and
  composite images and the effects of later managed-slot writes.
- Verified original build reports, full source commit and relevant file hashes,
  provisional differences where permitted, pinned core/toolchain/patch inputs,
  commands, and a manifest of file byte counts and SHA-256.
- Actual board/backend, resident version, ABI, API-table symbol/address/size,
  target/format identity, memory/slot/code bounds, and supported upload limits.
- Matching ABI/image/facade/layout definitions, compatibility rules, notices,
  evidence references, and clearly separated executable/parser-only fixtures.

Derive hashes, addresses, and identities from actual artifacts. Reject missing,
ambiguous, stale, mismatched, unsafe-path, or partial inputs. Test corruption,
report/source consistency, deterministic archive contents where supported, and
failure before output. Export to an explicitly selected new destination without
device access, flashing, publication, or fake resident values.

Firmware-side acceptance: the compiler can verify this package and derive one
real compatible Nano descriptor without connecting a board. The SDK independently
validates installation/deployment evidence. An artifact hash establishes byte
identity, not publisher authentication or installed-flash attestation.

The initial experimental implementation is now available:

```sh
python3 experiments/managed_sketch/export_resident.py --help
python3 experiments/managed_sketch/export_resident.py \
  --resident-dir path/to/exact/resident-build \
  --firmware-commit exact-40-character-git-commit \
  --output path/to/new/resident-export
```

It checks all reported project sources against that commit, requiring all tracked
C/C++ library inputs and the board/ABI/version/build-script identities. The only
ignored historical entry is `src/.DS_Store`; its bytes are not read or exported.
The existing installed core/toolchain identities remain declarations, not a full
upstream inventory or clean reproduction. Missing inputs or inconsistent source,
board, upload limits, memory budgets, artifact hashes and ELF/UF2 bytes fail
before output creation. Symlinked, special and oversized inputs are rejected.

The bounded ELF32 ARM reader derives the unique global readonly 40-byte FAPI-v1
symbol and its eight Thumb pointers, checks executable section containment and
flash LOAD mapping, then compares every initialized ELF flash byte with the
resident-only UF2 using the production UF2 checks. Resident LOAD permissions may
be RWX; the API object's section must remain readonly. This does not change the
compiler's stricter stateless module rules. Composite images are rejected.

Historical schema 1 exports exactly `resident.elf` (up to 8 MiB), `resident.uf2` (4 MiB),
`build-report.json` and `resident-facts.json` (256 KiB each), plus a checksummed
inventory `manifest.json`. Facts include actual firmware/board/profile/backend,
ABI/API table, target/capsule limits, artifact/report hashes, commit/source hashes,
producer/UF2-validator tool hashes and explicit qualification/provenance limits.
No compiler recipe or physical-device identity is assigned. Existing output
directories are refused; a filesystem write failure can leave incomplete files
but never reports successful export. No hardware or installation operation occurs.

Firmingo-Compiler's `tools/dev.mjs import-resident` requires an independently
reviewed manifest hash, checks bytes/schema and UF2/FAPI, attaches its pinned
recipe and emits metadata into a new directory. It copies no firmware images and
does not populate the default catalogue. See the
[local export/validation evidence](../evidence/resident-compiler-export.md).

## Compatibility and qualification

Version device ABI/image changes deliberately. Document whether existing modules
remain compatible, require recompilation, or become unsupported after an update.
Keep stable resident identity separate from a composite image's initial whole-file
hash; replacing a managed slot changes that image without necessarily changing
the resident. Hello cannot establish exact resident ELF bytes.

Native GCC proof results do not qualify Clang/LLD WASM output. Verify portable
vectors first, then deploy two compiler-produced sketches through an actual
wrapper in a separately authorized hardware workflow. Record compiler/recipe,
resident/artifact identities, board, wrapper/runtime/OS, installation, startup,
and useful behavior separately. Current Nano restrictions and missing Pico
managed support remain explicit.

## Migration sequence

1. Pin existing firmware contracts and build evidence in the migration inventory.
2. Maintain the experimental exact-build exporter and focused host tests; CI
   exports every newly compiled experimental resident against its checked-out commit.
3. Supply verified facts to the compiler's Nano recipe and descriptor pipeline.
4. Retain independent firmware validation while wrappers integrate the compiler.
5. Qualify the exact cross-project combination before expanding support claims.

The inventory and compatibility plan are maintained during the transition in
[FirmingoKit's migration document](../../../FirmingoKit/docs/compiler-migration.md).
That sibling link is a local research convenience, not a build dependency.
Historical evidence and existing contract pins are preserved; this documentation
update does not declare a new tested firmware revision or select a new compiler ABI.

## Experimental consumer package

Firmingo-Compiler now binds the exact schema-2 CI export from Firmingo commit
`e0a79d1e47aa90d5dda02ccd284d40b9c2922c12`, manifest `9246a7da…`, into consumer
package `0.1.0-nano-managed-experimental.1`. Descriptor `c44d25ef…` retains actual
API `0x10022c9c`, original ELF/UF2/report identities and all six contract resources.
The complete package manifest is
`d042dc7d3869949d8e311fff28c5d66327ba090ba2ec8ff149e3a12019d8ea8c`.

FirmingoKit's experimental catalogue consumes that static descriptor. Its clean
SDK package-preparation step supplies the reviewed compiler for offline default
resource loading. Firmware owns the facts and independent validator; it acquires
no dependency on compiler/SDK checkout or package version. Exported images remain
evidence inputs, not an automatically selected installation release. Existing
upstream core/toolchain provenance limits and lack of installed-image/hardware
qualification remain explicit. No device, ABI, protocol or firmware source changes
are made by this packaging increment.

The first prepared SDK's default compiler produces the known 316-byte GPIO and
392-byte console capsules (`958b861b…`, `b372c00e…`). Independent production image
validation accepts both at `0x10022c9c` and rejects the old `0x10022ca0` binding
(`wrong_api`). This is offline compiler/validator compatibility, not execution,
installation, authorization or hardware qualification. No native firmware rebuild
or device operation is claimed for this packaging increment.

## Versioned source resources

The exporter now defaults to schema 2, adding a checked source contract and exact
ABI/image/facade/entry/linker/license resources from the selected firmware commit.
See [source contract](resident-source-contract.md) for fields, compatibility and
legacy-import behavior. Actual build facts and descriptor identities retain their
existing meaning; no runtime ABI or wire change is introduced.
