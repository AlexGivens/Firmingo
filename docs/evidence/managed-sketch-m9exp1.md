# Nano managed-sketch M9 upload build — 2026-09-27

This is **compile/native evidence only**, recorded before the subsequent
[M9 hardware tests](managed-sketch-m9exp1-hardware.md). At that time, the Nano remained on the exact
`m8exp4` image for the user's planned overnight check. No M9 resident was
flashed, no hardware FMGO install was attempted, and no board power/reset or
host network settings were changed during this work.

## Implemented candidate

`m9exp1` exposes target 1 / `nano-managed-v1` through FMGO TCP 7420. Release
references omit the optional upload service and retain `targets:[]` and
unsupported-upload behavior. The candidate removes M7's private TCP 7421
listener, retaining the same Nano one-sector installer and narrow capsule ABI.

The production Session validates the control schemas and type-4 binary chunk
prefix, then calls a platform-independent upload-service interface. The Nano
adapter owns one 4 KiB RAM stage, one boot-lifetime status record, and a queued
commit. Actual pause/erase/program/readback runs from the resident loop outside
USB callbacks and the Ethernet/lwIP lock. Console ownership and active upload
are mutually exclusive. Physical D2/GPIO25-to-GND authorization is checked at
begin, commit, and before install. There is no cryptographic peer authentication,
signed-image requirement, rollback, persistent transaction journal, or promise
of recovery from arbitrary sketch faults.

The [protocol extension](../protocol.md#experimental-nano-managed-upload-extension-m9)
defines accepted, verified, committing, installed, boot_confirmed, failed, and
aborted states. New core-1 generation acknowledgement confirms setup and one
loop returned; it does not attest to useful behavior or long-term stability.
Pre-commit disconnect aborts the stage. After commit, disconnect leaves the
operation running and a new session queries status without replaying commit.
Reboot loses the RAM record, leaving the old outcome uncertain to the host.

## Commands and results

- `cmake --build build/native --parallel 2` and
  `ctest --test-dir build/native --output-on-failure`: **20/20 targets passed**.
  The new upload target has 12 cases using the production Session, CapsuleStage,
  and SketchSwitch with fake flash/runtime ports. It covers target/schema/arm
  rejection, digest/header errors, duplicate/out-of-order/overflow/oversize
  chunks, every chunk split, partial response writes, console/owner exclusion,
  disconnect/timeout disposal, lost commit response, distinct install/start
  states, stalled core, arm removal, and partial install failure.
- Configured `build/native-m9-sanitize` with `FIRMINGO_SANITIZE=ON`,
  `FIRMINGO_UPLOAD_FUZZ=ON`, and `FIRMINGO_PROTOCOL_FUZZ=ON`; built with
  `--parallel 2`; full CTest: **23/23 targets passed**. The separately selected
  upload mutation case ran 1000 cases with seed `0x4d394655`. After strengthening
  arm/schema checks, the two affected upload targets were rebuilt and rerun:
  **2/2 passed**. Mutations are parser tests, not hardware safety evidence.
- `.venv/bin/python -m pytest -q tests/host`: **178 passed**, including nine M9
  host tests. Fake peer tests verify exact wire encoding, transaction/hash
  matching, pre-commit disposal, and read-only reconciliation after lost commit
  responses or changed boots. They are not Nano install passes.
- Pinned `build_resident.py`, Blink A/B `build_module.py`, and Blink B
  `build_composite.py`: passed. Regeneration and `verified_capsule` accepted both
  exact module/build reports. `fmgo_upload.py --help` and transfer help ran.
- `git diff --check`: passed. The new shared hex fixtures include a synthetic
  validation capsule, which is not executable firmware and must never be flashed.

## Exact build

Arduino CLI 1.5.1 and patched Arduino-Pico 6.0.0, Pico SDK USB, NCM-only deferred
startup, IPv4, 125 MHz, Small optimization, 16 MiB no-filesystem Nano layout.
Artifacts are isolated under ignored `build/managed_sketch/m9exp1/`; its reports
record source hashes. The resident builder now enforces the existing 1 MiB flash
and 192 KiB static-RAM engineering budgets, and modules enforce the installer's
4 KiB capsule cap.

| Artifact | Size / SHA-256 |
| --- | --- |
| Resident | 134,328 flash bytes; 96,672 static RAM bytes |
| Resident ELF | `8539fb1b26f016c0bf8290f8404d40afb785099419de84f120f02ee7c401c06d` |
| Resident UF2 | `658ce6e056e88d3f1d0dfdd4a936263676eaee16d4eb2271b3c400a8c2fb95bb` |
| Blink A capsule | 688 bytes; `4e92dbd5a9d24952c712bd32d8348a6d0a3de5c54441e42bd8908822564ad3b7` |
| Blink B capsule | 692 bytes; `6b5ce8500f31ee623a04d4a5264e5543281cd569ab5149f7c30e54712d518a81` |
| Blink B initial composite | 307,712 UF2 bytes; `f33d894bb9ed02ddf410cff36550cecd217c889d0b9c95a5f6f1244e121ca4aa` |

The API table is at `0x10022ca0`. The builder checked the 58-byte core-1 park
function wholly in SRAM at `0x200000c0`, with no helper calls or PC-relative
loads. Disassembly shows Session dispatch reserves 2,036 local stack bytes plus
20 pushed register bytes; dispatch_upload adds 316 local plus 20 pushed bytes
when nested. This is not a worst-case call/interrupt stack bound. Hardware stack
and combined upload/network behavior remain unmeasured for this image.
The only compiler warning class was the pre-existing Arduino-Pico
`WiFiClient::write(uint8_t)` hidden overload. The NINA module is unused.

## Reference compile matrix

Application and UART references for both declared boards were compiled directly
with the same pinned CLI/core overlay and the FQBNs in their baseline manifests.
The recorded commands use `--warnings all`, `--library` pointing to this repo,
`FIRMINGO_NET_C=77`, deferred-start, and NCM-only flags. Per-build command JSON,
logs, sizes, warning lists, and hashes are in
`build/managed_sketch/m9exp1/reference-matrix/`. All passed engineering budgets;
these are portability builds, not packaged or hardware-qualified release images.

| Board / profile | Flash bytes | Static RAM bytes | UF2 SHA-256 |
| --- | --- | --- | --- |
| Nano / application | 128,808 | 91,432 | `de5bf4c2d1b12aaaddc9964cce2643d28169ee852b702e0b0919a589704c5b6a` |
| Nano / UART | 129,512 | 90,648 | `3933a121d4ff00576dc0ac0e975c648bf648eadfb012c65730e308568d3a7a71` |
| Pico / application | 128,716 | 91,400 | `2f8452efa4c7a354ab428d1d9f14e95b9fab26b40f568441a84b9311d7b2f913` |
| Pico / UART | 129,420 | 90,624 | `cf9aa6fa11077eb8e1a5f528c402e9ac72a111035d8217fedd47793572994977` |

## Remaining hardware work

The subsequent hardware record tracks the user-authorized tests before the
overnight M8 observation. The planned hardware sequence is to select the
recoverable Nano again, install
and fully read back the exact M9 initial composite through ROM, then verify
identity and console before enabling upload. With D2 physically armed, use the
opt-in harness for two consecutive Blink replacements, stage-only disconnect,
and read-only status reconciliation. Confirm LED behavior and exact console
bytes separately from `boot_confirmed`. Power-cut cases require separate
explicit manual runs and uncertainty/recovery records. None of these M9 hardware
cases, iOS operation, or actual SDK/IDE workflows is claimed here.

The exact passing M8 image was also preserved under ignored
`private-evidence/originals/m8exp4-recovery/`, SHA-256
`545967f1606f0c366c6bba57640ccea68e620c6ad23463534da328cd82722bea`.
Current M9 source does not reproduce that historical M8 build; old source-hash
checks must not be bypassed by claiming a new M8 build. The preserved raw image
is a known recovery artifact, with hardware scope in the separate M8 record.
