# Managed-sketch SDK handoff — experimental M9

This is the firmware-side handoff for a separate Swift SDK and IDE. The current
reference is Nano RP2040 Connect `0.1.0-m9exp1`, FMGO protocol 1 over TCP 7420.
The [protocol](../protocol.md) is authoritative; this guide describes how to
consume it and reproduce the constrained build. Neither an SDK/IDE integration
pass nor iPadOS FMGO support is established by these files.

## Contract and portable vectors

| Source | SDK use |
| --- | --- |
| `docs/protocol.md` | Framing, schemas, limits, ownership, upload state and interruption contract |
| `tests/fixtures/protocol-v1/*.hex` | Base hello, serial binary frame, diagnostics, unsupported reset |
| `tests/fixtures/protocol-v1/managed-upload/*.hex` | M9 target hello, request/chunk frames, all upload states, authorization error |
| `experiments/managed_sketch/image.h` and `abi.h` | Capsule/ABI constants; the SDK transfers an already compiled capsule |
| `docs/evidence/managed-sketch-m9exp1.md` and `managed-sketch-m9exp1-hardware.md` | Exact build and measured hardware scope in the full repository |

The hex files are complete frames, encoded as whitespace-separated hexadecimal
bytes. Decode hex to bytes before parsing the 16-byte FMGO header. Managed-upload
vectors use request ID 9, synthetic device/boot IDs, and a **non-executable**
264-byte capsule bound to synthetic API address `0x1002154c`. They are parser
inputs, never uploadable firmware. The base hello fixture advertises no targets;
the M9 hello fixture advertises one. Both cases must work in the SDK.

Export a checksummed ZIP without board access:

```sh
mkdir -p build/managed_sketch/sdk-handoff
.venv/bin/python experiments/managed_sketch/export_sdk_contract.py \
  --output build/managed_sketch/sdk-handoff/m9-contract.zip
```

The destination must be new. `manifest.json` records byte counts and SHA-256 for
every included file. Identical source inputs produce identical ZIP bytes within
the same Python/zlib runtime. The archive includes this guide, the protocol,
ABI headers, and all protocol-1 fixtures. It contains no UF2, ELF, executable
capsule, or claim that an SDK passed tests. Documentation links to other files
refer to the full Firmingo repository.

## Connection and console

1. Use an explicitly selected numeric endpoint (`192.168.77.1:7420` for this
   test setup). Bonjour/discovery is not implemented. IP address is not identity.
2. Send hello once per connection and check `protocol_major`, `device_id`,
   `board_id`, backend, negotiated payload, and target capabilities before
   controls or bytes. Associate transaction IDs with both device ID and boot ID.
   The version string is self-reported; it does not attest to an exact UF2 hash.
3. Decode arbitrary TCP fragments and coalesced frames. Route type-2 replies by
   request ID while delivering type-3 bytes separately; serial data can precede
   a control reply. Do not apply UTF-8 or newline conversion to the byte stream.
4. Open channel 1 explicitly. A second owner receives `busy`. Serial frame
   payload is a big-endian channel ID followed by bytes; request ID is zero.
   Acknowledged TCP delivery alone does not prove sketch consumption.
5. Close the console before starting an upload. A close/disconnect disposes
   unsent queues; reconnect opens a fresh session with no byte or command replay.

The two sketch queues hold 256 bytes each. Pre-open sketch output is rejected,
so `Sketch A ready`/`Sketch B ready` from setup is not guaranteed to appear in a
monitor opened later. A console-open epoch is acknowledged at a sketch loop
boundary. The SDK must preserve partial writes and apply bounded backpressure.
Baud controls are not supported for this application backend; `Serial.begin`
does not set network speed. `device.reset` is unsupported.

## Upload and uncertain outcomes

The M9 hello target is:

```json
{"id":1,"format":"nano-managed-v1","max_size":4096,"max_chunk":1024,"authorization":"physical-d2-gnd"}
```

An IDE supplies compiled bytes plus matching build reports, closes its console,
and requests an explicit upload. D2/GPIO25–GND must be physically bridged for
begin/commit/install. REC–GND is the separate ROM recovery procedure.

| Step | SDK action | What the result establishes |
| --- | --- | --- |
| Begin | Send target, board, format, total capsule size and whole-capsule SHA-256 | `accepted`, new upload ID, zero received; RAM only |
| Transfer | Type 4: big-endian upload ID + capsule offset + bytes; await each correlated response | Exact offset accepted; no flash write |
| Finish | Send upload ID and target | `verified`; header and both digests passed in RAM |
| Commit | Send once after verification | `committing`; installation queued |
| Reconcile | On a fresh matching-device/boot session, query ID/size/hash/status | `installed` means readback passed; `boot_confirmed` means setup and first loop returned |
| Console | Open again and compare expected behavior/bytes independently | Useful sketch behavior, beyond startup acknowledgement |

Chunk size is at most `min(max_chunk, max_payload - 8)`. Offsets are relative to
the capsule, not flash addresses. Do not deduplicate/replay chunks implicitly.
Only the originating session may send chunks, finish, abort, or commit; another
session may query status. Disconnect before commit aborts staging.

Distinguish the response envelope from the transaction outcome:

- `ok:false` rejects that request; follow `error.code`.
- `ok:true` with `result.state:"failed"` successfully reports a failed
  transaction. Inspect `result.error` and conservative `slot_touched`.
- `installed` without `boot_confirmed` is not a confirmed sketch start. A
  nonreturning setup/loop can prevent confirmation.
- A lost commit reply is uncertain. Query status; **never resend commit**. A
  changed boot or superseded RAM record cannot establish the old outcome.
  Present uncertainty instead of inferring success or claiming rollback.

The resident retains only the newest transaction until reboot/next begin. IDs
can repeat across boots. Handshake and incomplete frames each have a five-second deadline;
idle sessions expire after 30 seconds. The park deadline is one second. The
reference harness polls confirmation for five seconds, which is a host test
deadline, not a firmware guarantee that arbitrary sketch startup completes.
Use bounded waits and require explicit action after an uncertain result.

## Pinned reference build and metadata

The supported proof uses a tiny local Arduino-shaped facade and named Blink
sources; it is not a full Arduino-Pico application build. Writable module
`.data`/`.bss`, general Arduino libraries, interrupts, USB/NINA/filesystem APIs,
and arbitrary source-path compilation are unsupported. The capsule setup and
loop must return for a later live replacement to park core 1. Only the exercised
D13/GPIO6 pin use belongs to the current GPIO compatibility claim.

The resident uses Arduino CLI **1.5.1** and patched Arduino-Pico **6.0.0**;
the module uses `pqt-gcc/5.0.0-9576866`, Cortex-M0+, Thumb, `-Os`, freestanding
GNU C++17, no exceptions/RTTI/standard startup, the local facade, and `module.ld`.
The Nano FQBN in `boards/nano_rp2040_connect/baseline.json` selects Pico SDK USB,
16 MiB no-filesystem flash, IPv4, 125 MHz and Small optimization. Resident flags
select deferred NCM-only startup and subnet `192.168.77.0/24`; NINA is unused.
Provision the pinned overlay using [development instructions](../development.md).
Do not silently substitute Arduino Mbed or an installed newer core.

To build a named proof against the **existing exact M9 resident ELF**:

```sh
PROOF_TOOLS=build/firmware/nano_rp2040_connect/application/usb-toolchain/data/packages/rp2040/tools/pqt-gcc/5.0.0-9576866/bin
PROOF_RESIDENT=build/managed_sketch/m9exp1/resident
.venv/bin/python experiments/managed_sketch/build_module.py \
  --sketch blink_a --resident-elf "$PROOF_RESIDENT/artifacts/firmingo_managed_resident.ino.elf" \
  --toolchain-bin "$PROOF_TOOLS" --output-dir build/managed_sketch/sdk-example
```

The full resident/module/composite build recipe is in the experimental
[README](../../experiments/managed_sketch/README.md). Initial ROM UF2 installation
is separate from capsule upload and smoke. The builder reads the API table
symbol from the exact resident ELF rather than inventing its address. For the
hardware-tested M9 build that symbol is `0x10022ca0`; it may change on rebuild.
The code starts at fixed XIP address `0x10200100`; the 64-byte little-endian FMS1
header is padded with zeros to offset 256. The complete capsule is 257–4096 bytes.

| Module report field | Required integration check |
| --- | --- |
| `board`, `image_format`, `target_id` | Match selected board and advertised upload target |
| `resident_elf_sha256`, `api_address` | Match the known installed resident build's ELF/API binding |
| `capsule_bytes`, `capsule_sha256` | Match actual `.fms` length and whole-capsule hash; cap at target max size |
| `code_bytes`, `code_sha256`, entry addresses | Match capsule header/code validation; code digest differs from whole-capsule digest |
| `source_sha256`, `compiler` | Preserve build provenance and pinned compiler selection |

The resident report separately identifies its ELF and resident-only UF2. The
initial composite UF2 has a different hash because it also contains the sketch.
Compile-report status strings describe build results, not live hardware state.
Treat local build provenance and the device's self-reported identity separately.
An API address match alone is not a cryptographic resident identity check.

The Python hardware harness validates local source/artifact reports before
transfer. It remains the reference for protocol behavior, not a Swift client:

```sh
.venv/bin/python experiments/managed_sketch/fmgo_upload.py --help
.venv/bin/python experiments/managed_sketch/smoke.py --help
```

iPadOS needs a separate compilation route producing this same linked capsule
and metadata. No local iPad CLI or compatible remote compiler has been tested.

## Cross-project acceptance checklist

| Check | Current evidence / required next result |
| --- | --- |
| Shared vectors | Native tests compare full frames with production Session/stage/switch; archive checksum checks are host-only |
| SDK framing and ownership | Run these vectors in the separate SDK; exercise split/coalesced reads, partial writes, busy and reconnect |
| Upload reconciliation | SDK must send one commit, handle loss by read-only status, reject mismatched device/boot/hash and report terminal failure |
| Two IDE uploads and console | Firmware harness A→B and exact echo passed; repeat through actual IDE/SDK with exact versions/build records |
| Cold replug and power interruption | One pre-commit verified-stage power cut passed; flash-write power loss and overnight reliability remain untested |
| macOS routing | Prior M8 harness Wi-Fi coexistence passed; repeat during actual M9 IDE traffic |
| iPad workflow | Record real authorization/lock/denial, automatic addressing, compile path, two uploads, exact bytes and Wi-Fi access |
| Beginner walkthrough | Supply setup/IDE instructions to a new user; record steps, failures and recovery, rather than treating a harness as an IDE |

The SDK owns networking/framing, identity/target validation, session ownership,
byte delivery, and upload/status reconciliation. The IDE owns editing, compiler
integration, board choice, progress and user actions. The firmware validates and
installs capsules; it does not compile source. ROM recovery remains independent.
