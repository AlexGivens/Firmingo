# Selected Nano FMS2 initial installation

On 2026-10-15 the user selected the previously tested Nano RP2040 Connect,
confirmed that its software needed no preservation, and entered REC/GND ROM
mode with D2 disconnected. Pinned `pqt-picotool/5.0.0-9576866` identified the
same RP2040 B1 / 16 MiB flash identity observed by the prior read-only hello.
Raw identities and ROM logs remain in ignored local evidence. The generic ROM
volume alone was not used to establish the board model.

[The input/readback record](nano-fms2-initial.json) pins source/export identities.
Resident-only UF2 `1ef76622…` is the exact schema-3 FMS2 upload candidate from
Firmware `97be7bb…`, version `0.1.0-statefulupload-exp1`. It retains the existing
constrained resident design and exposes target 2, `nano-managed-v2`, with
physical D2-to-GND authorization. This installation did not rebuild firmware.

The new offline `compose_stateful_initial.py` checks the actual UF2 FAPI2/state
facts and invokes the production native FMS2 validator before creating an
initial image. Three regression cases build the actual native checker, then
test exact resident/sector preservation, wrong API/state/size/capsule rejection,
independent input pins and isolated CLI/no-overwrite behavior. Synthetic fixtures
remain non-deployable. CI now composes an image from its actual native ARM proof
and exact resident export, without hardware or SDK dependencies.

The initial State A capsule came from actual signed, sandboxed macOS SDK/WASM
execution and matches the independently compiled 544-byte artifact `537ce260…`.
The SDK and Compiler outputs create the identical 316,416-byte composite
`205e2fcf…`. Every resident payload byte is preserved; the whole first 4 KiB
managed sector contains that checked capsule plus `0xff`. Other flash is outside
the image. Native validation checks initialized data/BSS, guarded generations
and host boot dispatch; it does not execute ARM instructions.

Before writing, the selected ROM identity and exact composite hash/size were
checked again. `picotool load -v` and a separate `picotool verify` passed. Two
independent `save -r … -t bin` operations then read all 154,112 resident bytes
and all 4,096 managed-sector bytes. Every byte matched the corresponding UF2
payload, with no uncovered gaps in either range. Reboot occurred only after
these comparisons. This proves the included image regions at that moment,
not all 16 MiB flash, cryptographic attestation or future contents.

One bounded read-only Mac hello then matched the selected identity, new version,
application channel and sole target 2: maximum 4096 bytes, chunk 1024 bytes,
`physical-d2-gnd`. The user moved the board normally to the iPad without D2/REC,
reported the checked-hello status and D13 off. The iPad's own SDK run produced
both exact State A/B capsules; both passed the production native validator.
That is initial setup and read-only inspection evidence. SDK managed upload,
startup, state replacement, console integrity and manual reboot observations
are recorded separately as they occur; earlier ABI-one passes cannot qualify
FMS2. This ROM maintenance workflow is not an SDK installation-backend pass.

Local checks: `tools/dev.py test --sanitize` passes 23 CTest targets and 250
host cases. The initial sandbox invocation passed native targets but failed
19 existing loopback tests on socket-bind permission; the enabled-socket rerun
passed. The focused composer suite passes all three cases. The new CI composer
command also passes locally with the actual native ARM proof. Workflow YAML
parses and all bash steps pass syntax checking; actionlint is unavailable.
No new firmware compile matrix or remote CI pass is inferred from these checks.
