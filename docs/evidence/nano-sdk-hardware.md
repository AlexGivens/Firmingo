# Scoped Nano SDK console/GPIO checks on iPad and iPhone

On 2026-10-15 the selected Nano completed two managed-sketch uploads through
FirmingoKit on each of the physical Apple devices below. This follows the
[verified initial ROM setup](nano-sdk-initial.md); it did not rebuild the resident
or change firmware source, ABI, protocol or toolchain pins.

| Host | Exact test environment | Observed result |
| --- | --- | --- |
| iPad Pro 11-inch (3rd generation) | iPadOS 27.0.1 (24A446), development-signed NanoQualification build 3 | Console/GPIO RAM stage, one host commit attempt and fresh boot-confirmed status per operation; exact 256-byte echo and manual D13 off-to-on transition |
| iPhone 15 Pro Max | iOS 27.0 (24A437), identical signed app | Equivalent console/GPIO outcomes and manual LED transition |

The exact resident is exported Firmware `e0a79d1…`, ELF `5b3116cd…`,
resident-only UF2 `35678c95…`, API `0x10022c9c`, descriptor `c44d25ef…`.
Prepared SDK `4edc6aa…` consumes the reviewed compiler package `d042dc7d…`.
Host source is `796c776…`. Actual exported GPIO/console files match the known
316/392-byte capsules `958b861b…`/`b372c00e…`; production firmware validation
accepts them and rejects wrong-API/corrupt-code inputs. Full identities, source/
signature provenance, original report hashes and sanitized transaction records
are retained in the canonical SDK evidence at commit `01dd0e1…`:

- [iPad evidence](https://github.com/AlexGivens/FirmingoKit/blob/01dd0e11af16fa723cc05b8ad2f40fc8d17b4da6/docs/nano-hardware-ipad.json)
- [iPhone evidence](https://github.com/AlexGivens/FirmingoKit/blob/01dd0e11af16fa723cc05b8ad2f40fc8d17b4da6/docs/nano-hardware-iphone.json)

Each host compiles offline, inspects the selected identity/capabilities, explicitly
records reviewed ROM resident evidence, then stages the console capsule with D2
grounded. RAM verification precedes the one commit attempt. A `committing` reply
is followed by fresh `boot_confirmed` reconciliation with `error=none` and
`slotTouched=true`; startup is distinct from the later exact 256-byte echo.
The user explicitly begins the next operation and presses RESET, observes D13
off after a fresh hello, then re-associates the new boot and installs GPIO.
Boot confirmation is followed by the user's D13-on observation. Both per-boot
records and final journals (23 iPad/22 iPhone events) were independently checked.
The intermediate installed state was not separately sampled; host journals do
not replace a packet trace counting wire requests.

The initial iPad inspection failed with Network is down; its later explicit
attempt passed. A separate local association-prerequisite rejection also remains
in that journal. The first iPhone inspection failed around the user's reported
access prompt; a later explicit read-only retry passed without reset/reconnect.
These failures remain failures. Their causes and clean cold-attachment timing
are not established. The app's status/field labels were corrected from actual
user feedback before the managed-upload checks.

App installation and own-run collection used the paired network while Nano
occupied the Apple device's USB port. Uncached Internet routing was not tested.
The user removed the D2 upload arm and left REC disconnected after completion.
PCB revision and cable/hub model were not recorded. This is narrow experimental
SDK/resident/compiler compatibility on the selected board and hosts, not a 0.1.0
release qualification, general Arduino/Pico support, minimum-OS, GPIO electrical
timing, throughput/soak, full authorization/cold-attachment matrix or power-loss/
rollback result. Compiler and SDK checkouts remain unnecessary for normal firmware
builds/tests; the links above are evidence references only.
