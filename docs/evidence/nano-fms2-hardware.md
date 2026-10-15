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

This qualifies these exact experimental combinations. It does not promote the
default firmware/catalogue or qualify a complete SDK initial-installation
backend, minimum OS, general Arduino/libraries, macOS managed deployment,
interruption/power loss, rollback or recovery. Those production gates remain
separate. Earlier ABI-one evidence retains its original scope.
