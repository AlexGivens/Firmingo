# Managed resident build profile

The first release-engineering step toward
[Nano-first 1.0](integration/production-v1.md) is an ordinary build command:

```sh
python3 tools/dev.py build --board nano_rp2040_connect --firmware managed
```

Use the same explicit `ARDUINO_CLI` and `ARDUINO_CONFIG_FILE` setup as
[development](development.md). The command requires CLI 1.5.1 and Arduino-Pico
6.0.0, prepares the existing verified USB-startup overlay, then invokes the
existing `experiments/managed_sketch/build_resident.py`. It does not build an
extra application image to obtain an overlay. Pico is rejected before toolchain
work. This profile currently fixes subnet 77 and uses `resident/build.log`;
nondefault subnet and verbose overrides are explicitly rejected.
CLI selections found through `PATH` are resolved to an executable path before
invoking the legacy resident builder, which accepts a filesystem path.

Outputs are under `build/firmware/nano_rp2040_connect/managed/`:

- `profile-report.json`: starts failed, becomes compile-tested only after all
  checks; always records experimental qualification and no hardware test.
- `resident/report.json`, `resident/build.log`, `resident/artifacts/`: existing
  resident report, compiler diagnostics, ELF and resident-only UF2.
- `usb-toolchain/`: isolated patched core/config; installed sources are not patched.

The profile invalidates a previous resident report before setup so a failed
toolchain check cannot leave an earlier success usable as the new build. On
compile/validation failure its own report remains failed. Older artifacts can
remain for diagnosis; use successful current reports and validated hashes rather
than selecting a file merely because it exists.
After successful toolchain setup, previous exported images are removed before
compilation. A compile that supplies a report without new images fails validation.

Independent checks use Firmware's actual ELF/FAPI and UF2 readers: board/FQBN,
upload target, source identities, numeric memory budgets, artifact hashes, API
table and executable pointers, and every initialized ELF flash byte in the UF2.
Profile/overlay tool source hashes are checked again before success. The existing
resident builder also verifies the core-1 park function lies in SRAM with no
helper calls or PC-relative loads. The reported core/toolchain versions and
overlay checks are not a complete inventory or clean reproduction of all
upstream toolchain inputs; that provenance work remains a release gate.

This command builds the **existing experimental stateless ABI-one/M9 resident**.
It does not change firmware identity, ABI, flash layout, upload limits or compiler
pins. It is not a production 1.0 image, stateful-sketch implementation or hardware
qualification. Normal `package` and `flash` continue to accept their documented
reference profiles; they do not silently acquire managed-image support.

For an exact committed source export, use the existing explicit exporter against
`resident/` with that commit. Its stricter committed-source/export checks remain
separate from development builds with working-tree source changes.

CI's managed job uses this same entry point and exact checked resident for the
existing export, GCC proof capsules and initial-image composition. Application
and UART builds remain in the independent reference matrix. Failed driver,
profile and resident logs/reports are retained through `if: always()` uploads.
Local tests are `python3 -m pytest tests/host/test_managed_profile.py -q` and
`python3 tools/dev.py test --sanitize`. Artifact fixtures are non-executable;
neither native tests nor compilation supplies physical evidence.

[Initial local evidence](evidence/managed-profile.md) records the actual build,
20 sanitizer CTest targets, 217 host cases, exact-commit export and compiler import.
