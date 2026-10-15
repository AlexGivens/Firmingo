# Nano-first production 1.0 acceptance plan

Accepted direction on 2026-10-15. This is a work plan and release gate, not a
support claim or authorization to flash, interrupt hardware, or publish assets.
The current firmware version remains 0.1.0 beta. The compiler and SDK retain their
current experimental descriptors and qualification records.

## Intended supported journey

On one Arduino Nano RP2040 Connect, a developer can install the selected Firmingo
resident, establish a reviewed device association, compile a supported sketch
offline, upload once, confirm startup and useful behavior, edit/rebuild/replace
it, and exchange an ordered binary console stream. macOS, iPadOS and iOS support
must name tested environments. Mac-assisted initial installation is an acceptable
first-release route for iPad/iPhone; direct installation on those platforms is
not promised. Recovery must remain available when networking or sketch execution
is unavailable.

Firmware, compiler and SDK have independent release versions. A tested matrix
binds exact resident/image/ABI, compiler service/recipe/catalogue and SDK versions.
Protocol, artifact format and interface versions are distinct from product SemVer.

## Useful sketch subset before interface freeze

| Capability | 1.0 goal and acceptance |
| --- | --- |
| Entry points | One `setup()` and repeated `loop()` through resident-owned execution on the sketch core |
| Mutable state | Bounded initialized and zero-initialized globals/static storage survive loop calls; every new sketch generation and reboot initializes its own state before setup. This is RAM state, not nonvolatile persistence |
| GPIO/timing | An explicitly listed facade with board-correct GPIO output, `millis()` and `delay()`; D2 remains reserved for physical upload authorization. Additional calls need their own contract/tests |
| Console | Bounded binary read/write/availability through resident queues, preserving all 256 byte values, ownership, backpressure and explicit loss/rejection diagnostics |
| Source bundle | One primary `.ino`, explicit helper `.cpp` and `.h` files, bounded relative paths/count/bytes, deterministic job ordering, local include resolution and diagnostics referring to the original file |
| Limits | Publish measured/validated flash, RAM, stack, queue and host resource limits; reject incompatible or oversized work before deployment |

The current ABI-one/FMS1 stateless, single-file proof does **not** implement the
mutable-state or source-bundle goals. Its 4096-byte upload and 3840-byte code
limits remain enforced. Do not relax its validators or silently reinterpret its
reserved header fields to accept a new format.

The next stateful contract needs separately versioned image/ABI definitions,
resident-owned RAM allocation and linker rules, initialized-data and zero-fill
bounds, generation initialization, and independent malformed-image tests. Choose
arena/stack sizes from the actual resident link map and runtime measurements.
Keep resident memory, queues, core-0 state and upload authorization outside the
sketch allocation. This RP2040 design does not provide a security sandbox for
arbitrary native sketch code; document the owner-authorized execution model and
measured fault/recovery behavior.

Initial scope excludes arbitrary third-party Arduino libraries/cores, dynamic
allocation, C++ runtime constructors/exceptions, sketch-owned USB/interrupt
registration, conventional whole-image Arduino compilation, and Pico managed
support. These remain separate explicit feature increments. Do not advertise
background compilation or automatic rollback without implementation and evidence.

## Ordered implementation and release gates

| Gate | Owner | Required result |
| --- | --- | --- |
| V1-FW-BUILD | Firmingo | One ordinary managed build entry point, pinned overlay, actual ELF/UF2 comparison, source/artifact identities, memory checks and durable failed-build records. Current implementation still builds the experimental resident |
| V1-STATE | Firmingo, then Compiler/SDK consumption | Versioned stateful image/ABI, bounded RAM ownership and initialization on boot/replacement; independent native/parser tests plus actual compiler outputs. Retain the old contract explicitly during migration |
| V1-SOURCE | Compiler, then SDK adapter | Portable immutable source bundle, deterministic preprocessing/compile/link, per-file diagnostics, bounded local includes and versioned serialized requests. No host filesystem/library discovery |
| V1-PACKAGE | Firmingo and Compiler | Fresh pinned resident build/export; exact recipe and catalogue; reviewed binary with corresponding source/notices and independently trusted inventory; reproducible provisioned consumer |
| V1-SDK | FirmingoKit | Normal resource/API use, verified installation-derived association, signed sandbox initial installation where offered, supported update/recovery and a minimal documented consumer; no normal manual compiler/hash imports |
| V1-HOST | FirmingoKit | Real compiler cancellation/deadline/error cleanup, remaining compile/link interruption and worker termination, aggregate app/WebKit resource budgets and real pressure; test declared OS minimums or narrow official support |
| V1-DEVICE | Firmingo and FirmingoKit | Exact candidate's two-sketch compile/upload/startup/console/GPIO journey, longer traffic/backpressure/reconnect tests, first-use permission UX, 20 recorded cold connections per claimed host, locked/denied/allowed paths and uncached Internet coexistence |
| V1-RECOVERY | Firmingo and FirmingoKit | Explicit selected-board tests for pre-commit disconnect, lost commit reply, app restart, reboot and selected erase/program interruption stages; tested ROM recovery and honest uncertain outcomes, with no automatic write replay |
| V1-RELEASE | All three | Stable public contracts and compatibility/deprecation policy, provisioned CI with actual WASM cases, fresh consumer/archive checks, release notes/source/notices and exact tested asset hashes |

Hardware qualification uses one coherent candidate, not a mixture of earlier
resident, wrapper and test-host versions. Preserve older successes and failures
as historical evidence. A checksum or successful hello is not installed-image
attestation. A physical authorization prompt is separate from the app's
local-network permission prompt.

For first-use permission handling, keep SDK network work bounded while allowing
the person time to read the system prompt and then explicitly continue/retry.
Do not make a five-second prompt response an onboarding requirement or add
implicit reconnect/write replay. Test both prompt approval and timeout/recovery.

## Current baseline and next increment

The exact experimental Nano resident and the reviewed WASM package already pass
scoped SDK console/GPIO uploads on recorded iPad/iPhone hosts. Compiler lifecycle,
deadline and host-memory samples have separate evidence. Updated read-only
permission checks record iPhone first-prompt success and iPad timeout/explicit
retry; the latter is not first-prompt success. These results do not qualify the
new stateful format, production profile, full installation, cold matrix, aggregate
memory or Internet/recovery gates.

The first increment adds [the managed build profile](../managed-profile.md) using
the existing resident and independent artifact validators.
V1-STATE now has a [candidate FMS2 validator and native initialization proof](stateful-image-v2.md).
An [opt-in allocation/link proof](stateful-link-proof.md) now reserves real RAM,
derives candidate API/linker facts and validates an independently linked ARM
capsule through host initialization. Runtime lifecycle integration and a versioned
deployment export come next, before compiler recipe/packaging and SDK validation.
V1-SOURCE can progress against explicit versioned fixtures while hardware work
remains separate.

See [compiler ownership/exports](compiler-contract.md),
[release publication gates](../releasing.md), and
[historical cross-project hardware evidence](../evidence/nano-sdk-hardware.md).
