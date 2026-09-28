# M10 firmware-side SDK handoff — 2026-09-27

This is contract, test-vector, export-tool and build-recipe evidence. No Swift
SDK/IDE source was changed or tested, and no iPadOS compilation/workflow pass is
claimed. The Nano was not flashed, reset or sent network commands in this work;
the latest installed image remains M9/Blink B from the separate
[hardware record](managed-sketch-m9exp1-hardware.md). M8 restoration still
requires the user's explicit request.

## Delivered contract

The [handoff guide](../integration/managed-sketch-sdk.md) maps the firmware
contract into SDK framing, identity/target selection, console ownership,
opaque binary bytes, exact build matching and upload/status reconciliation.
It includes a pinned named-proof build recipe and the cross-project acceptance
checks still needed. The protocol now includes a byte-offset FMS1 header table
and links the actual M9 hardware scope. These documentation changes do not
change runtime framing or request/response schemas.

Eleven new complete hex vectors extend the seven existing managed-upload
vectors: M9 hello request/response, begin/chunk acknowledgements, queued commit,
installed/start-confirmed status, explicit abort, disconnect abort, install
failure status and unarmed refusal. In particular, successful status retrieval
of a failed transaction has `ok:true` with `state:failed`; it is distinct from
a rejected request. Existing vectors were left unchanged.

A new native case feeds shared requests through production Session,
CapsuleStage and SketchSwitch and compares complete response bytes. Fake flash
and generation ports select installed/confirmed/failure states; they do not
replace the separate Nano observations. All fixture capsule code is synthetic
and non-executable.

`export_sdk_contract.py --output NEW_ZIP` packages the guide/protocol, ABI/image
headers and all protocol-1 fixtures, with source byte counts/hashes in
`manifest.json`. It refuses an existing destination and has no network, board,
flash, or reset operations. It excludes executable capsules and firmware images.

## Commands actually run

- `cmake --build build/native --target fmgo_upload_tests --parallel 2` and
  `ctest --test-dir build/native -R '^fmgo_upload$' --output-on-failure`:
  **1/1 target passed**, now containing 13 cases including the new vector case.
- Built `fmgo_upload_tests` and `fmgo_upload_fuzz` in the existing
  `build/native-m9-sanitize` configuration, then ran
  `ctest --test-dir build/native-m9-sanitize -R '^fmgo_upload' --output-on-failure`:
  **2/2 passed**, including 1000 bounded mutations with seed `0x4d394655`.
- `.venv/bin/python -m pytest -q tests/host/test_managed_sdk_contract.py
  tests/host/test_fmgo_upload.py`: **11 passed**. The two exporter cases check
  byte-identical archives for identical inputs, manifest/source integrity,
  vector lengths/types/states, absence of executable images, and preservation
  of existing destination data. After final documentation changes, the exporter
  cases were rerun: **2 passed**.
- `export_sdk_contract.py --help`: passed. Exported
  `build/managed_sketch/sdk-handoff/m9-sdk-contract-2026-09-27.zip`, then checked
  ZIP integrity, every manifest digest/length, and equality to current source.
  The archive has **31 files**, including **25 hex vectors** (18 managed-upload).
  Size: **33,359 bytes**; SHA-256:
  `a5d58af6e52111102b8241b813ff44f1298e019397d5e22e3a8866e8ccf4bbbf`.
- Ran the guide's `build_module.py --sketch blink_a` command with the pinned
  overlay's ARM tools and exact existing M9 resident ELF, outputting to
  `build/managed_sketch/sdk-example`. It compiled without warnings and produced
  a 688-byte capsule with SHA-256
  `4e92dbd5a9d24952c712bd32d8348a6d0a3de5c54441e42bd8908822564ad3b7`,
  matching the previously hardware-tested Blink A capsule. No resident rebuild
  or new hardware install was performed.
- Current source/artifact checks accepted both original exact M9 Blink A/B
  reports via `verified_capsule`. Relative links from the guide, protocol and
  vector README resolve; `git diff --check` passed.

## Remaining acceptance

Firmware contract publication is complete for this experimental handoff. The
separate SDK must consume the vectors and implement framing, target matching,
stream ownership and uncertainty correctly; the IDE must integrate its editor
and compiler. Actual macOS/iPadOS end-to-end workflow and beginner walkthrough
remain untested. A local iPad compiler/remote build service is not implemented
here. The current builder accepts named proof sketches, not arbitrary source
paths or general Arduino libraries. M9 power loss during erase/program and
overnight reliability remain open.
