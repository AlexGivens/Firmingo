# Candidate Nano state allocation and ARM link — 2026-10-15

Result: the opt-in resident reserves an actual 4096-byte BSS arena. A separately
linked Cortex-M0+ counter sketch produces an FMS2 capsule with initialized and
BSS globals; production C++ validation/initialization passes on guarded host RAM.
**No ARM execution or board operation ran.** Runtime integration and deployment
exports remain open. See the [contract/workflow](../integration/stateful-link-proof.md)
and [machine-readable receipt](stateful-link-proof.json).

## Actual build facts

Pinned Arduino CLI 1.5.1, Arduino-Pico 6.0.0 with the repository's deferred NCM-only
overlay, pqt-gcc `5.0.0-9576866` (`arm-none-eabi-g++ (GCC) 16.1.0`). Native checker
built on macOS with ASan/UBSan through CMake. The new driver checks source hashes
again after compilation and validates initialized ELF bytes against the UF2.

| Fact | Observed |
| --- | --- |
| Candidate flash / static RAM | 134,376 / 100,768 bytes, within 1 MiB / 192 KiB budgets |
| Candidate API table | ABI 2, 52 bytes at `0x10022c98` |
| Owned state array | `0x20007914`, 4096 bytes, allocated writable NOBITS/BSS symbol |
| BSS end / heap start | `0x2001897c` / `0x200189a0` |
| Heap limit | `0x20040000`; nominal span 161,376 bytes, not measured free RAM |
| Linker stack reservations | 2048 bytes per core in scratch RAM; peaks unmeasured |
| Stateful sketch | 124 code bytes, 4 initialized bytes (`7`), 4 BSS bytes |
| Capsule | 384 bytes; SHA-256 `276afceb134d726d021f86470f0d18cb76591d135a0a2e7ee7efdf1564a249fe` |
| Candidate resident UF2 | SHA-256 `2d5bcc216d59fe6aee3eac84a99e61c76ed4d8d15b9c41aa876525a12f0bfb67` |

Full ELF/table/module/checker/source identities are in the JSON receipt. Same
numeric API addresses in different resident builds do not imply compatibility;
exact versioned build/artifact binding remains necessary.

## Commands and outcomes

The passing local proof used:

```sh
.venv/bin/cmake -S . -B build/native-sanitize -DFIRMINGO_SANITIZE=ON
.venv/bin/cmake --build build/native-sanitize --target managed_stateful_check --parallel 2
.venv/bin/python experiments/managed_sketch/build_stateful_proof.py \
  --cli '/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' \
  --config-file build/firmware/nano_rp2040_connect/managed/usb-toolchain/arduino-cli.yaml \
  --validator build/native-sanitize/managed_stateful_check \
  --output-dir build/stateful-link-r2
```

The checker verified exact initialized data, zeroed BSS and unused arena, guards,
active-generation retention, explicit-stop replacement/reset and rejection of a
corrupted real capsule. The selected sketch contains real ARM loads/stores, but
host mutation models the running-loop state; its setup/loop were not executed.

The first attempt (`build/stateful-link-r1`) successfully built the resident and
linked the sketch, then rejected the module's LOAD layout: GNU ld included ELF
headers before `.text`. Explicit `PHDRS` now excludes those bytes. The failed
receipt and log remain local, with hashes retained in the JSON.

`.venv/bin/python tools/dev.py test --sanitize` passed **21/21 CTest targets** and
231 host cases with local loopback enabled. After adding failed-build receipt and
existing-output preservation checks, the final host invocation
`.venv/bin/python -m pytest tests/host -q` passed **233/233 cases**, including all
16 new independent ELF/receipt cases. The first sandbox invocation passed native
checks but blocked 19 socket fixtures with `PermissionError`; the authorized
loopback-enabled rerun passed. These are local fixtures, not hardware tests.

The ordinary `tools/dev.py build --board nano_rp2040_connect --firmware managed`
also passed with the isolated pinned CLI/config. Its default ABI-one API remained
`0x10022c98`, flash/static RAM 134,320/96,672 bytes, with 152,832 initialized ELF
bytes matching UF2. Its UF2 hash remains the preceding local baseline
`f2fbe7d76e378a61ce09ebe7ca6a69c2f3efaccc96e3ba16ff72222c22d008ba`.
A direct check confirmed the old exporter rejects the candidate report status.
The default resident also exported successfully against exact committed source
`c2f568b48ed6709115dbeb027e6859568af7a6d0`, with manifest SHA-256
`bd8aad112f8b05fd705d375c9fc7d608c0dcb46d0d2ec56a7945f462a2a5e9bb`.
Existing consumer pins and installed firmware were not changed.

Workflow YAML parsed through Ruby YAML and all bash blocks passed `bash -n`;
`actionlint` was unavailable. The managed CI job now runs the same candidate
command and retains its logs, actual binaries, map and receipts.
[CI run 38724190142](https://github.com/AlexGivens/Firmingo/actions/runs/38724190142)
at `c2f568b48ed6709115dbeb027e6859568af7a6d0` reports all ten jobs successful,
including the new allocation/link step. Public run, job and step statuses were
inspected; raw remote logs/artifacts were not retrieved. The exact local counts
and byte identities above remain separate evidence.

## Remaining

Implement opt-in boot/replacement lifecycle ownership and bounded slot views,
then export the versioned deployable contract for Compiler/SDK adoption. Measure
aggregate memory use and qualify actual ARM setup/loops, reset, replacement,
interruption and recovery with explicit board selection. Hashes are integrity
checks; the RAM initializer is not a sandbox for arbitrary native code.
Pre-existing unrelated `AGENTS.md` changes are preserved and excluded.
