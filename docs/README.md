# Documentation index

Start with the [repository README](../README.md) for current scope and
[board status](boards.md) for supported targets. Use [development](development.md)
for the pinned toolchain, [releasing](releasing.md) for package format and gates,
and [testing](testing.md) for hardware commands. [Protocol](protocol.md) is the
FMGO development contract; [USB startup](usb-startup.md), [local-only networking](local-only-network.md),
[cold attachment](cold-attachment.md), and [iOS qualification](ios-qualification.md)
cover the corresponding implementation and checks.

[Nano-first 1.0 acceptance](integration/production-v1.md) defines the agreed useful
sketch subset and cross-repository release gates. The
[managed build profile](managed-profile.md) begins release engineering using the
existing experimental resident; it does not promote its hardware/support claims.
The [candidate FMS2 contract](integration/stateful-image-v2.md) implements portable
state validation and generation initialization with native-only fixtures. The
[allocation/link proof](integration/stateful-link-proof.md) adds a real resident
RAM arena, candidate API/linker facts and an independently linked ARM capsule.
The [runtime increment](integration/stateful-runtime.md) adds opt-in board boot
and native replacement tests. The [target-2 contract/adapter](integration/stateful-upload-contract.md)
adds an exact-commit versioned export and opt-in FMS2 upload build. Compiler/SDK
adoption and hardware qualification remain open.

The separate [unfinished-flash checkpoints](integration/flash-checkpoint-qualification.md)
prepare explicit test-only residents and an identity-bound, one-shot native
harness. Manual power cuts and SDK ROM recovery remain independently qualified
physical operations; these images cannot be exported as compiler residents.

The experimental [managed-sketch compiler/SDK handoff](integration/managed-sketch-sdk.md)
provides the M9 contract, portable vectors, pinned reference build recipe,
and cross-project acceptance checklist. The
[compiler contract plan](integration/compiler-contract.md) defines firmware-owned
build facts, the Firmingo-Compiler/FirmingoKit boundary, and the implemented
experimental exact-build exporter. [Export evidence](evidence/resident-compiler-export.md)
records source/artifact checks and independent validation of two actual WASM capsules.

[Evidence index](evidence/README.md) separates actual prior observations and
raw logs from current release claims. [Research index](research/README.md)
contains historical proposals and pre-release design notes; treat them as
background, not instructions or proof of the current firmware's behavior.
