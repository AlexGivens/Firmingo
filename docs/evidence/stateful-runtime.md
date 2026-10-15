# Candidate FMS2 runtime — 2026-10-15

Result: the opt-in Nano **boot-only** resident compiles/links and its actual ARM
capsule passes production runtime initialization/dispatch checks on the host.
Ten native lifecycle cases exercise the real core pause and replacement driver
with fake flash. No device connection, flash or ARM execution was performed.
See [runtime semantics](../integration/stateful-runtime.md) and
[exact machine-readable evidence](stateful-runtime.json).

## Observed checks

- The ordinary sanitizer entry point passed 22 CTest targets and 236 host cases
  with authorized loopback access. After the final initialization-failure case,
  all 22 native targets passed again; the lifecycle executable has ten cases.
- Boot tests cover every truncated mapped fixture length, hostile length fields,
  no writes/publication on invalid boot, one setup per token, repeated-loop RAM
  retention, fresh-runtime reset and explicit recovery from an invalid boot.
- Replacement tests cover private snapshot capture, busy/invalid/unarmed input,
  wrap-safe park timeout, arm removal before/after park, stop-before-flash,
  successful readback/init before release, and smaller replacement tail clearing.
- Injected flash-begin, partial erase, second-page program, capsule readback,
  erased-tail readback and missing-readback failures all leave execution disabled
  after the stop boundary. A separate wrong-arena case fails initialization even
  after good flash readback and publishes no entries. A new explicit request
  recovers after a partial write; idle polls do not replay it.
- Standalone pinned Cortex-M0+ C++11 runtime compilation passed with warnings as
  errors. The embedded build compiles the same production implementation.
- Both the allocation-only proof and ordinary ABI-one resident still build and
  validate. The old exporter rejects both candidate report modes. The default
  resident UF2 remains SHA-256
  `f2fbe7d76e378a61ce09ebe7ca6a69c2f3efaccc96e3ba16ff72222c22d008ba`.

The first Arduino runtime build failed because directly included implementation
files shared anonymous helper names `failed` and `u32`. Runtime-specific names
resolve the amalgamation collision; the failed log is retained and hashed.
The final outputs are under ignored `build/stateful-runtime-r3` and
`build/stateful-runtime-allocation-r3`, with source, binary and log hashes in JSON.

## Actual boot candidate

| Fact | Observed |
| --- | --- |
| Toolchain | Arduino CLI 1.5.1, patched Arduino-Pico 6.0.0, pqt-gcc 5.0.0-9576866 / GCC 16.1.0 |
| Flash / static RAM | 132,768 / 96,272 bytes; within existing 1 MiB / 192 KiB engineering budgets |
| Candidate API / state | ABI 2 table at `0x1002261c`; 4096-byte BSS array at `0x20006784` |
| Initialized ELF bytes compared with UF2 | 150,992 |
| Capsule | 380 bytes: 120 code, 4 initialized data, 4 BSS |
| Capsule SHA-256 | `36b7408621cd1fe36fb01f3197ab2b3d5a0a10fe627dd95fe45aeefa2b051ca6` |
| Resident UF2 SHA-256 | `9bf43c273d658f30d124f27d150c0945f6742dfe8f46b50513fedc931491a931` |
| Network uploads | Disabled in boot candidate; no new FMS2 target |

The counter proof now alternates GPIO from the loop counter; adding both counters
would leave parity constant. This is compiled code only, not an observed LED
transition. The native checker models entry calls against actual linked initial
data; it never jumps into ARM instructions. Memory totals describe the boot-only
resident, **without** a replacement staging instance or future upload adapter.
Linker heap span and stack reservations are not measured free RAM or peak use.

## Reproduction

After the ordinary profile prepares the pinned overlay:

```sh
.venv/bin/python tools/dev.py test --sanitize
.venv/bin/cmake --build build/native-sanitize --target managed_stateful_check --parallel 2
.venv/bin/python experiments/managed_sketch/build_stateful_proof.py --boot-runtime \
  --cli '/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' \
  --config-file build/firmware/nano_rp2040_connect/managed/usb-toolchain/arduino-cli.yaml \
  --validator build/native-sanitize/managed_stateful_check \
  --output-dir build/stateful-runtime-r3
```

Use a new output directory on rerun. The same driver without `--boot-runtime`
selects allocation only. CI builds both modes, separately compiles the runtime
ARM object and runs the ordinary native/host suites.
[Implementation CI at `4e491bc`](https://github.com/AlexGivens/Firmingo/actions/runs/38727327344)
reports all ten jobs successful, including both candidate build steps. Public
run/job/step statuses were inspected; raw remote logs/artifacts were not retrieved.
The source inputs match exact committed blobs at
`4e491bc1ca53b5c800ccbabb8e52417b442f8c86`; the default resident also exports
successfully against that commit. Local counts and bytes remain separate evidence.

## Remaining

Export the complete versioned contract, then wire a firmware upload adapter with
ownership, authorization, retained status and protocol fixtures. Compiler and SDK
must consume exact versioned facts before deployment. Qualify real setup/loops,
replacement, interruption and recovery on a selected board. A failed replacement
has no persistent boot latch or rollback: reboot validates actual slot contents.
Existing consumer pins, installed firmware and unrelated `AGENTS.md` changes
remain untouched.
