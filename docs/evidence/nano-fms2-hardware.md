# Selected Nano FMS2 SDK hardware checks

On 2026-10-15 the same selected Nano RP2040 Connect passes the constrained FMS2
sequence through FirmingoKit's development-signed NanoStateful build 1 on both
an 11-inch iPad Pro (3rd generation), iPadOS 27.0.1 (24A446), and an iPhone
15 Pro Max, iOS 27.0 (24A437). [Initial ROM evidence](nano-fms2-initial.json)
pins the actual resident from Firmware `97be7bb…`, resident-only UF2
`1ef76622…`, full included-region readback and known initial State A capsule.

Prepared SDK `e0bed36…`, host `84acce6…`, consumer `1bbc0cc1…`, service
`a7648688…`, recipe `nano-stateful-clang-22.1.8-v1-experimental` and explicit
descriptor `dacbf259…` remain unchanged throughout. Actual WASM compilation on
each host produces State A / State B capsules of 544/548 bytes,
`537ce260…` / `505c7238…`; both pass the independent production FMS2 validator.
The SDK owns full provenance, raw-journal hashes and per-transaction evidence:
[iPad record](https://github.com/AlexGivens/FirmingoKit/blob/main/docs/nano-fms2-ipad.json),
[iPhone record](https://github.com/AlexGivens/FirmingoKit/blob/main/docs/nano-fms2-iphone.json).

| Check | iPad | iPhone |
| --- | --- | --- |
| Exact target-2 SDK stage/commit/fresh startup | State B then A; one host commit attempt each; `boot_confirmed`, no error | Same |
| In-boot replacement | Both transactions share one boot | Both transactions share one boot |
| Initialized data, startup BSS loop counter, advancing counters | Seeds 23/7, zero startup loop counter, two nonce-correlated snapshots per sketch | Same |
| Exact binary echo | All 256 byte values twice | All 256 byte values twice |
| Manual D13 observation | Initial A off, B on, A off | Same |
| Separate manual RESET | Changed boot; A seed 7, zero startup loop counter and advancing counters | Same |
| D2 upload arm after completion | User confirmed removed | User confirmed removed |

The snapshots observe the selected initialized data/BSS loop counters, not an
exhaustive hardware RAM scan. Native checks separately validate the complete
arena. LED observations are manual; no electrical timing, throughput or soak
claim follows. First inspection succeeds on both devices, but permission timing
and the wider cold-attachment/routing matrix are not established. Own-app report
retrieval uses paired Wi-Fi while Nano occupies each device's USB port; this is
not an uncached Internet routing check. Raw physical device/boot identities stay
in local evidence.

The functional sequence above qualifies these exact experimental combinations.
It does not promote the default firmware/catalogue or qualify a complete SDK initial-installation
backend, minimum OS, general Arduino/libraries, macOS managed deployment,
interruption/power loss, rollback or recovery. Separately scoped interruption
results follow below; broader production gates remain separate. Earlier ABI-one
evidence retains its original scope.

## Separate interruption and recovery checks

The same selected resident and compiler inputs now pass the scoped sequence on
the recorded iPad through development-signed **NanoRecovery** build 1. Test-host
source is FirmingoKit `f5fb34f5fb5ce2dce1a30ecd28d5efb05ff8b6a6`; the unchanged
prepared SDK is `e0bed36…`. The executable SHA-256 is
`4f9c9a6dc70fbdbc64797f6969971a98adb4ec9d7cc717afb01383ddd5dd245c`.
The [pinned iPad SDK evidence](https://github.com/AlexGivens/FirmingoKit/blob/bd772b258a660f6305bd0fa6bb458ab3658c739d/docs/nano-fms2-interruption-ipad.json)
has SHA-256 `cf02b212ed0eed43c44fe2deb9d48cd9fa62f341be9df08b2db19526f744d687`
and preserves both app runs, journal/checkpoint/transaction hashes, decoded state
snapshots, actual withheld reply and process-restart receipts.

| Selected fault/check | iPad observed result |
| --- | --- |
| Verified B RAM stage, explicit socket close without abort/commit | Fresh `aborted`, `disconnected`, untouched slot; retained A counters continue in the same boot |
| Verified B RAM stage, operator USB unplug/wait/reconnect before commit | Saved transaction stays `unknown(bootChanged)`; A survives with seed 7, zero startup BSS and advancing counters |
| Explicit retirement of RAM-only host context | Unknown device result preserved; new operation requires inspection and reviewed association |
| One real B commit with its actual reply withheld by the test proxy | One observed commit frame and matching real `committing` response; SDK `uncertainCommit`; durable commit intent |
| SIGKILL and normal app relaunch with a different PID | Same checkpoint restored query-only; fresh matching `boot_confirmed`, B seed 23 / zero startup BSS / advancing counters and exact 256-byte echo; no host commit replay |
| Fresh ordinary A upload after recovery | Distinct transaction in the recovered B boot, one commit, fresh startup/state/echo pass; manual D13 changes from on to off |

Both freshly exported capsules retain the known hashes and pass the production
native validator. The original pre-restart journal stays unchanged. The user
confirms D2 removed afterward, with REC disconnected. This tests power removal
**before** flash writing and deliberate
application-level reply loss after a real accepted commit. It does not test power
loss during erase/program, corruption, rollback or complete SDK ROM recovery.
No firmware, compiler asset, public SDK API or production support claim changes.

### iPhone follow-through

The identical signed NanoRecovery app separately passes the same scoped sequence
on the recorded iPhone 15 Pro Max/iOS 27.0. The
[pinned iPhone SDK evidence](https://github.com/AlexGivens/FirmingoKit/blob/bd772b258a660f6305bd0fa6bb458ab3658c739d/docs/nano-fms2-interruption-iphone.json)
has SHA-256 `2d8a89e5c5216934c8396c575a42e71c71310f66d26ce4e1b56a7001a711a140`.
It pins an original 39-event journal and a new 22-event journal after actual
SIGKILL/relaunch. Pre-commit disconnect returns aborted/disconnected with an
untouched slot and continuous A state. Pre-commit USB power-cycle preserves A
and leaves the old transaction unknown after boot change; explicit host-context
retirement preserves that unknown result.

One observed real B commit reply is withheld, producing SDK uncertainty with
durable commit intent. The new app run restores the unchanged checkpoint for
queries only and passes matching fresh boot-confirmed B, initialized data/zero
startup BSS/advancing counters and exact binary echo without host commit replay.
Its later ordinary A stage/commit/startup/state/echo passes in the same Nano boot
with a distinct transaction. D13 is observed on for B and off for A, and the user
confirms D2 removed. The original journal is unchanged and all capture prefixes
and capsule identities are independently checked.

The new iPhone journal also retains one prerequisite rejection after inspection
and before successful final association, before A staging. It does not identify
the triggering action, and the operator does not recall it. The later A stage
and commit each occur once; the earlier rejection is not counted as a successful
attempt. These observations retain the same flash-write power-loss, rollback,
ROM recovery, minimum-OS and broader production limitations as the iPad result.
