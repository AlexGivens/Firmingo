# Selected Nano manual flash interruption and SDK ROM recovery

On 2026-10-16, the user-selected recoverable Nano RP2040 Connect completed the
explicit [checkpoint procedure](../integration/flash-checkpoint-qualification.md).
[Exact ledger](nano-flash-checkpoints.json) pins the two real fault residents,
native GCC capsules, initial composites, local tests, raw-capture hashes and
physical observations. Firmware source is `38c24bc26e1e2d2206dfd81c9b3a1c5898722afd`.
The user had manual USB unplugging only; no controlled switch or electrical trace
was used. Raw identities and interface captures remain local and uncommitted.

| Selected case | Observed result |
| --- | --- |
| Real sector erase, deliberate hold, operator sees slower D13 pulses and removes power | Exactly 4096 erased bytes; all 154,112 resident bytes unchanged; production validator rejects slot |
| Real first-page program, deliberate hold, operator sees slower pulses and removes power | Exact selected 256-byte header page plus 3840 erased bytes; resident unchanged; valid header hash, invalid payload hash; production validator rejects actual declared 380-byte span |
| Explicit boot after erased slot | First selected read-only hello matches test resident/target 2 |
| Explicit boot after first-page slot | First five-second TCP connect times out; later separately selected hello matches without reset/reconnect; failure retained, cause unresolved |
| New full SDK recovery after each managed checkpoint | Fresh operation/attachment, transferComplete, fresh release hello and exact 256-byte echo; separate ROM verification/save matches all 144,128 payload bytes |
| SDK host paused after accepting 65,536 UF2 bytes, operator removes power | Durable writeStarted; only owned PID/command-verified host SIGKILLed, exit 137 |
| New host process restores saved SDK context | Different PID, identical original record, unknown outcome; no target/session/token or replay |
| ROM readback after partial SDK transfer over distinct prior resident | First 32,768 flash payload bytes match new recovery image; remainder matches prior resident; exactly 19,500 changed bytes; neither complete image matches |
| New full SDK recovery from that mixed image | Fresh operation/attachment and full transfer; fresh hello/exact echo; independent final full payload readback matches |
| Separate read-only reconciliation of original interrupted SDK record | Remains outcomeUnknown despite matching hello/echo; original checkpoint unchanged |
| Final normal reboot | Fresh SDK release hello/exact echo pass; Nano left on ordinary reviewed 0.1.0 application with D2/REC removed |

The first-page initial timeout is not counted as a pass. Read-only diagnostics
found the Nano USB device, active host interface/address/route and no ARP entry at
that observation. A later hello does not identify the lost exchange, prove a race
or qualify consistent first-attempt attachment. No automatic reset, write retry
or commit replay hid this result. Both managed commits were sent once, after
synchronized intent, and no host status query was sent during either hold.

FirmingoKit's separate development-signed, hardened ARM64 macOS 27.0 command-line
host `86a319e…` performs complete-image transfers through public SDK APIs. It is
unsandboxed. [SDK evidence at 9d50d49](https://github.com/AlexGivens/FirmingoKit/blob/9d50d4945a9ec6dbd59854079359c3fe93e4417e/docs/nano-rom-recovery.json)
records all installation/progress/context/verification runs and the exact old
record's conservative unknown result. Firmware-owned pinned picotool only
provides independent identity/readback and test-image setup; it is not the SDK
writer under qualification. Recovery candidate UF2 is `2e2d6082…`, payload
144,128 bytes, SHA-256 `872bf96e…`. Its historical unsigned observed manifest,
original build-report-byte limitation and data effects remain explicit.

The recovery image replaces the fault resident with the ordinary Firmingo
application. The incomplete managed sector remains outside that image. This
recovers Firmingo communication; it does not restore the previous FMS2 sketch or
keep its compiler association valid. The mixed SDK image stayed in ROM until
recovery, with no attempted execution or unavailable-network claim for that image.

Local checks pass: **23 sanitizer CTest targets, 265 host tests**, both real
checkpoint/native-capsule/composite preflights, and ordinary ABI-one/FMS2 build
regressions. The ordinary UF2 hashes remain `f2fbe7d7…` and `1ef76622…` respectively.
Pinned upstream WiFiClient overloaded-virtual warnings remain visible in build
logs. The managed CI job builds both opt-in fault paths without hardware access.
[Actions run 38963164981](https://github.com/AlexGivens/Firmingo/actions/runs/38963164981)
reports all ten jobs successful for the exact source commit. Raw-log download
returned HTTP 403, so remote test contents were not independently verified;
this statement reports job conclusions only.

These are power cuts during unfinished installations, **between completed flash
commands**, plus an actual partial full-image overwrite and host restart. They
do not qualify power loss inside a flash-chip command, automatic rollback,
sandbox/minimum-OS execution, Pico, direct iOS/iPadOS ROM programming, initial
installation from non-Firmingo, broad production packages or general Arduino.
Compiler assets and earlier iPad/iPhone WASM/FMS2 evidence are unchanged.
