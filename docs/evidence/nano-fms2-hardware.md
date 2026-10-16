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

## Separate iPad interruption and recovery checks

The same selected resident and compiler inputs now pass the scoped sequence on
the recorded iPad through development-signed **NanoRecovery** build 1. Test-host
source is FirmingoKit `f5fb34f5fb5ce2dce1a30ecd28d5efb05ff8b6a6`; the unchanged
prepared SDK is `e0bed36…`. The executable SHA-256 is
`4f9c9a6dc70fbdbc64797f6969971a98adb4ec9d7cc717afb01383ddd5dd245c`.
The [pinned SDK evidence](https://github.com/AlexGivens/FirmingoKit/blob/57fb7b05cae1fac5eef7ae04382713b5b0e30f27/docs/nano-fms2-interruption-ipad.json)
has SHA-256 `85ce5019657115b0053b2358cc3c7dd30c97ce5ffb6eccf91029893165dbf6f0`
and preserves both app runs, journal/checkpoint/transaction hashes, decoded state
snapshots, actual withheld reply and process-restart receipts.

| Selected fault/check | Observed result |
| --- | --- |
| Verified B RAM stage, explicit socket close without abort/commit | Fresh `aborted`, `disconnected`, untouched slot; retained A counters continue in the same boot |
| Verified B RAM stage, operator USB unplug/wait/reconnect before commit | Saved transaction stays `unknown(bootChanged)`; A survives with seed 7, zero startup BSS and advancing counters |
| Explicit retirement of RAM-only host context | Unknown device result preserved; new operation requires inspection and reviewed association |
| One real B commit with its actual reply withheld by the test proxy | One observed commit frame and matching real `committing` response; SDK `uncertainCommit`; durable commit intent |
| SIGKILL and normal app relaunch with a different PID | Same checkpoint restored query-only; fresh matching `boot_confirmed`, B seed 23 / zero startup BSS / advancing counters and exact 256-byte echo; no host commit replay |
| Fresh ordinary A upload after recovery | Distinct transaction in the recovered B boot, one commit, fresh startup/state/echo pass; manual D13 changes from on to off |

Both freshly exported capsules retain the known hashes and pass the production
native validator. The original pre-restart journal stays unchanged. The user
confirms D2 removed afterward, with REC disconnected. iPhone interruption checks
remain pending. This tests power removal **before** flash writing and deliberate
application-level reply loss after a real accepted commit. It does not test power
loss during erase/program, corruption, rollback or complete SDK ROM recovery.
No firmware, compiler asset, public SDK API or production support claim changes.
