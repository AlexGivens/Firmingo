# Firmware contract for Firmingo-Compiler and wrappers

Ownership accepted on 2026-10-12. This document specifies the firmware side of
the compiler split; it does not implement an exporter, change the ABI/protocol,
or qualify compiler output. The existing
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

## Current export versus planned exact-build export

`experiments/managed_sketch/export_sdk_contract.py` currently exports documentation,
ABI/image headers, and protocol fixtures. Its manifest explicitly declares no
firmware images and non-executable fixtures. Preserve that contract and command;
the ZIP is not an exact resident-build export or executable test package.

The future non-flashing exact-build export should provide:

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
2. Define the required exact-build export; implement it as a later firmware task
   with focused tests and the corresponding CI/documentation updates.
3. Supply verified facts to the compiler's Nano recipe and descriptor pipeline.
4. Retain independent firmware validation while wrappers integrate the compiler.
5. Qualify the exact cross-project combination before expanding support claims.

The inventory and compatibility plan are maintained during the transition in
[FirmingoKit's migration document](../../../FirmingoKit/docs/compiler-migration.md).
That sibling link is a local research convenience, not a build dependency.
Historical evidence and existing contract pins are preserved; this documentation
update does not declare a new tested firmware revision or select a new compiler ABI.
