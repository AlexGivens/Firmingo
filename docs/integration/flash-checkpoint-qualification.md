# Selected Nano unfinished-flash checkpoints

This explicit destructive experiment prepares two **test-only** FMS2 residents.
It is separate from the default firmware, compiler descriptors, WASM assets and
prior iPad/iPhone qualification. The user selected a recoverable Nano and has
manual USB unplugging available. [Build/evidence ledger](../evidence/nano-flash-checkpoints.json)
records the exact images and later physical observations. The post-erase manual
power cut now has exact erased-sector/unchanged-resident readback, invalid-slot
network boot and a new full SDK recovery with exact echo/payload readback.
Both checkpoints and the actual partial-SDK host interruption/restart/new full
recovery now pass the selected byte/readback/echo checks. The first-page initial
TCP timeout remains preserved. [Dated outcomes](../evidence/nano-flash-checkpoints.md)
separate these observations from broader production gates.

First establish an independent recovery baseline. The development-signed macOS
FirmingoKit ROM host has installed the exact reviewed 0.1.0 application, then
matched fresh release hello and exact 256-byte echo. A separate identity-bound
ROM readback matches all 144,128 payload bytes. See the
[SDK procedure](https://github.com/AlexGivens/FirmingoKit/blob/main/docs/nano-rom-recovery.md).
That host is unsandboxed on ARM64 macOS 27.0. It does not qualify direct iOS/iPadOS
ROM programming, other boards, minimum OS or first installation from non-Firmingo.

## Timing and data effects

`build_stateful_proof.py --upload-runtime --flash-checkpoint erase|first-page`
selects a separate native GCC proof and image. The board still requires D2-to-GND
for target-2 network writes. REC is disconnected during ordinary operation.
The selected hooks run only **after** real `flash_range_erase` or
`flash_range_program` returns with XIP restored, outside lwIP/USB callbacks.
Core 1 stays parked and interrupts stay masked through the checkpoint. D13/GPIO6
pulses on/off in 250 ms hardware-timer waits, 120 pulses (about 60 seconds).
The hook then returns failure. It never continues to a subsequent flash command.
No network availability is expected during that hold.

| Selected checkpoint | Expected first managed sector after timely unplug |
| --- | --- |
| `erase` | All 4096 bytes `0xff`; no page program has started |
| `first-page` | Exact selected 256-byte capsule header page; remaining 3840 bytes `0xff` |

Both states must fail the production FMS2 validator. No healthy-sketch rollback
is promised. Only the first sector at `0x10200000` is affected by these managed
writes; separately read back the entire resident payload to establish that it
survived. The native proof's initial counter sketch blinks faster (100 ms between
LED changes) than the checkpoint. LED observations select a useful manual window,
but **ROM readback establishes the actual incomplete bytes**.

These checkpoints demonstrate power removal during an unfinished installation,
between completed chip commands. Manual unplugging cannot reliably establish
power removal inside a chip erase/program operation. No such claim follows from
the LED, accepted command, timeout or partial image. Controlled switching and
additional instrumentation would be required for that separate timing claim.

## Build and admission

Use `build_stateful_proof.py --help` with explicitly selected pinned CLI 1.5.1,
patched Arduino-Pico 6.0.0 overlay, native `managed_stateful_check` and a new output
directory. Both checkpoint modes require `--upload-runtime`; the lower-level
resident builder requires `--stateful-upload-proof`. Default builds have no
checkpoint. The ELF contains retained `FCP1`/version-one facts with kind 1/2 and
60,000 ms. The builder independently checks the actual mapped marker against
selected mode/report. Distinct wire versions end in `-statefulflash-erase-exp1`
or `-statefulflash-page-exp1`. `export_stateful.py` rejects fault images even if
a report is relabeled as normal. Do not import these images into the compiler.

The native counter capsule is independently linked against each new resident's
actual API/state addresses and checked by production C++. It is **not** a WASM
qualification capsule and must not use the old SDK association/catalogue pins.
`compose_stateful_initial.py` validates each paired resident/capsule and produces
an exact initial ROM composite including the full managed sector. Review its
whole-image digest before loading on the selected Nano, verify all included
regions in ROM, then separately reboot and inspect the expected identity.

`checkpoint_flash.py --help` is a separate explicit firmware harness. It requires
the selected board/device, proof directory, exact resident/capsule SHA-256,
matching native validator, new output directory and `--install-once`. It checks
all recorded source/artifact bindings, actual mapped resident facts and production
capsule validation before opening one selected FMGO connection. RAM verification
precedes a synchronized, directory-synchronized commit-intent record. It sends
commit once; reply loss stays uncertain. It does not query/reconnect/retry/reset,
replay writes or program ROM. Parser fixtures in normal tests are non-executable.

## Physical sequence

1. Keep D2/REC removed during selected ROM installation, full verification and
   reboot. Establish the actual initial counter behavior before fault arming.
2. Ask the operator to connect D2 to GND and be ready to unplug. Start only the
   reviewed checkpoint harness once readiness is confirmed. Unplug when D13
   changes to the slower checkpoint pulses. Leave USB disconnected and remove D2.
   If no pulses are observed, or the window expires, preserve that outcome; do
   not count a power-cut pass or silently repeat commit.
3. Reconnect directly into ROM using REC-to-GND, then remove REC. Recheck the
   exact flash ID. Before booting or writing anything, read the resident and
   complete managed sector. Compare every byte with the selected expected
   checkpoint and run the production FMS2 validator; retain the host journal and
   any unexpected result. A changed boot loses the RAM transaction journal.
4. Separately reboot if needed to inspect the resident's response to the invalid
   slot. A working hello is evidence of networking, not proof of exact resident
   identity or a complete nonexecution trace. Use ROM again for the new recovery.
5. Reacquire the reviewed complete 0.1.0 application and explicitly request a new
   **full SDK** ROM installation against the new attachment. No partial resume or
   replay is available. Require fresh release hello/exact binary echo and separate
   full payload readback before calling this scoped recovery successful.
6. Repeat the separately selected first-page case only after reviewing its own
   composite and bindings. Remove D2 after every upload phase. Preserve all
   failures; neither successful recovery nor a new attempt erases them.

An additional SDK `paused-install` test can stop after a host-accepted UF2 prefix,
kill/restart that host without replay, inspect actual flash and recover by a new
full install. Host filesystem acceptance is not flash progress. A same-image
partial overwrite cannot demonstrate unavailable firmware, so select a distinct
reviewed prior image and independently establish actual flash contents.

## Local and CI checks

`tools/dev.py test --sanitize` includes ordinary production runtime/flash failure
checks, new closed-mode/ELF marker/export admission checks and one-shot harness
checks. The managed CI job also compiles both checkpoint residents and native
proof capsules, retaining logs/artifacts. It never runs a hardware checkpoint.
Normal ABI-one and uninstrumented FMS2 paths remain separately built. Workflow
YAML and bash syntax are checked locally; remote CI results must be observed
before claiming a GitHub pass.
