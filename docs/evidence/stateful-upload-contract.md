# FMS2 contract and target-2 adapter — 2026-10-15

Implemented the [versioned contract/upload workflow](../integration/stateful-upload-contract.md).
The opt-in Nano resident now includes the real adapter, replacement snapshot and
runtime; native Session tests and host export checks pass. No device connection,
flash or ARM execution was performed. See [exact receipts](stateful-upload-contract.json).

- Ordinary sanitizer suite: **23/23 CTest targets and 247/247 host cases**.
  The adapter has seven behavior cases covering complete one-byte-read wire
  exchanges, owner/offset/state enforcement, disconnect/retained status, old-target
  rejection, exact-generation confirmation, authorization, lease expiry, park
  timeout and partial-program failure. The final exporter-only check passed 11
  cases after the committed resource inventory and metadata validation were finalized.
- Six independent mutation jobs passed. The new adapter job uses seed
  `0x46555032`, 2000 cases, with 1205 verified transfers and 795 rejected mutations.
  No callback wrote flash; guard bytes remained intact.
- Pinned ARM C++11 freestanding adapter compilation passed with warnings as errors.
  The actual resident/capsule build and production host initializer also passed.
- An actual-ELF/UF2 preflight export passed against an owned temporary Git source
  snapshot, producing 39 files plus the manifest. The canonical export against
  corrected source `97be7bbbe29ef1d18ec886ae6eea1a2d41905869` then passed, with
  manifest SHA-256 `90911f7d3684dab4123d36b9a018805cc36ae58cb197c318fe720cb18d434a65`.
  Every canonical output length/hash was checked against the manifest.
- The default ABI-one build passed and retained UF2 SHA-256
  `f2fbe7d76e378a61ce09ebe7ca6a69c2f3efaccc96e3ba16ff72222c22d008ba`.
  Existing exports, original vectors and consumer pins were not upgraded.

| Actual upload-candidate fact | Value |
| --- | --- |
| Toolchain | CLI 1.5.1, patched Arduino-Pico 6.0.0, pqt-gcc 5.0.0-9576866 / GCC 16.1.0 |
| Flash / static RAM | 135,472 / 104,904 bytes, within 1 MiB / 192 KiB engineering budgets |
| API / state arena | ABI 2 at `0x10023074`; 4096-byte BSS at `0x2000893c` |
| Capsule | 380 bytes, 120 code / 4 data / 4 BSS |
| Capsule SHA-256 | `f6f10055e506b84ae04ce94202442d99426337a3bf3f74a7655ef2203945ebca` |
| Resident UF2 SHA-256 | `1ef766226ce6a2c51c668562cd51a71ed0bfbe1634b76b54d704c7e710ca1b2f` |
| Initialized ELF bytes compared with UF2 | 153,984 |
| Target / limits | 2, `nano-managed-v2`, 4096 capsule / 1024 chunk bytes |
| Deadlines | 30,000 ms absolute staging lease; 1000 ms park wait |

The immutable header/API/linker contract is Firmware-owned. The exporter binds
committed sources/resources, report, actual readonly FAPI/FUP2 bytes, allocated
RAM, identity and initialized ELF/UF2 bytes. It rejects other build modes and
mismatches before creating output. Manifest schema 3 and contract version 1 are
separate from FMGO major 1 and FMS2/ABI 2. Compiler recipe IDs remain Compiler-owned.

During independent fixture authoring, the type-4 ID prefix was corrected to network
byte order and the wrong-target response gained its existing `message` field.
The initially failing logs are retained; final vectors match production Session.
The default FMS1 fixtures were untouched. YAML parsed and all bash blocks passed
`bash -n`; actionlint was unavailable. The first CI run at `e52fec6` failed the
Linux native/mutation builds: GCC rejected an if/declaration on one line as
misleading indentation. `97be7bb` splits the statements; seven adapter cases and
the 2000-case mutation target were rebuilt and passed locally afterward.

[Corrective CI run 38810907065](https://github.com/AlexGivens/Firmingo/actions/runs/38810907065)
reports all ten jobs successful at `97be7bb`. Raw logs confirm 23 native targets,
247 host cases, six mutation targets, and the actual 39-file FMS2 export. CI's
manifest SHA-256 is `41352ab025317c013413c7b5d77c43db95aada7daddfbac52fac7422759545c6`.
Remote artifact archive bytes were not retrieved; local artifact comparisons and
remote log evidence remain distinct, without a cross-host bit-identity claim.

Local commands are retained in the JSON and ignored `build/fms2-*` logs; the
[workflow](../integration/stateful-upload-contract.md#reproduce) gives rerun commands.
Next: explicit Compiler/SDK adoption and selected-board qualification. These are
engineering static-memory bounds, not measured dynamic memory stability or a
production 1.0.0 release. Unrelated `AGENTS.md` changes remain excluded.
