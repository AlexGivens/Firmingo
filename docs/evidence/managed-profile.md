# Managed profile foundation — 2026-10-15

Source `57ab0f4dd81f20ec59c2807430605da58bb0d72f` adds an ordinary Nano managed
build path and the shared [1.0 acceptance plan](../integration/production-v1.md).
[Exact local records](managed-profile.json) pin source, reports, ELF/UF2, logs,
export and compiler import. Existing unrelated `AGENTS.md` work was preserved.

The final command, with the documented explicit CLI/local configuration, passes:

```sh
.venv/bin/python tools/dev.py build --board nano_rp2040_connect --firmware managed
```

It builds the **existing experimental M9 stateless resident**. Flash use is
134,320 bytes, static RAM 96,672 bytes, within the existing 1 MiB/192 KiB caps.
The API is derived as `0x10022c98`; all 152,832 initialized ELF flash bytes match
the resident-only UF2. The 58-byte park function is checked at SRAM `0x200000c0`.
The build reports six inherited WiFiClient overloaded-virtual warnings; no new
firmware behavior or hardware claim follows.

The ordinary sanitizer entry point passes 20/20 CTest targets and the then-current
214 host cases. Additional omitted-input/stale-export regressions bring the final
host run to **217/217**, including 17 managed-profile cases. These exercise real
ELF/UF2 readers using independently authored non-executable fixtures, corruption
even with updated hashes, source omissions/drift, budgets/targets, stale outputs,
early setup failures and unsupported selections. No compiler or board is required
by these fixtures. Workflow YAML and every bash block parse; actionlint was absent.

The exact-commit exporter succeeds with eleven schema-2 payloads. An initial
abbreviated-commit invocation was rejected before output; the successful command
uses the full source commit. Compiler's existing explicit importer verifies the
manifest and source-contract mapping and derives experimental descriptor
`c7424af2603b5199c19273be042d27b2c3fefc1903da052ac617c220cfe51e47`.
No firmware image is copied into Compiler and the default catalogue is unchanged.

This is build/export/import evidence, not execution of the newly imported target
through WASM or hardware qualification. The installed Nano and prepared SDK
consumer pins remain unchanged. Full upstream toolchain reproduction, stateful
images, source bundles and the other production gates remain open. A local pass
does not establish GitHub CI success; remote validation is recorded separately
when available.
