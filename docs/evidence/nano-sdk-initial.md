# Selected Nano initial setup for SDK compiler qualification

On 2026-10-15 the user selected a physical Arduino Nano RP2040 Connect, reported
its ROM volume ready, and said its current software did not need preserving.
The pinned `pqt-picotool/5.0.0-9576866` reported one RP2040 B1 with 16 MiB flash.
Its ROM identity was checked before loading; raw device/boot identities remain
in ignored local evidence. The generic `RPI-RP2` volume alone did not select a
board model. [The machine-readable record](nano-sdk-initial.json) pins inputs,
tools and original local logs without publishing device identities.

The resident is the exported Firmware `e0a79d1…`, ELF `5b3116cd…`, resident-only
UF2 `35678c95…`, API `0x10022c9c`. All 49 reported project-source hashes still
match the current checkout. Export toolchain provenance limits remain unchanged;
this operation did not rebuild the resident. The initial 316-byte GPIO capsule
`958b861b…` came from the signed SDK offline app at `24993ce…`, prepared SDK
`4edc6aa…`, using the pinned WASM service. It sets pin 6 high with a short delay.
Firmingo's production C++ image validator accepts that capsule and the known
392-byte console capsule, and rejects a wrong API address and corrupted code.
This validates bytes; it does not observe GPIO behavior.

The offline `compose_initial.py` combines independently pinned inputs, preserving
every resident payload byte and filling the first managed 4 KiB sector with the
known GPIO capsule and `0xff`. This avoids booting preserved unknown sketch
contents. Other flash is outside the image. The 314,368-byte initial UF2 is
`4fb60a485492fc3e3e0f63cabd0cb8e9cd5b7a1f59dda4feb9b5dbee27b404f6`.
The exact composite was recreated and matched immediately before installation.
The existing ROM helper checked the selected identity, ran `picotool load -v`,
then `picotool verify` against the whole UF2, and rebooted only after both passed.
Readback establishes the image's payload regions, not all flash or future bytes.

One five-second-bounded FMGO hello then succeeded from macOS at the pinned test
endpoint. Device ID matched the selected ROM identity; hello reported Nano,
`0.1.0-m9exp1`, application channel and exactly one `nano-managed-v1` target with
4096-byte maximum, 1024-byte chunks and `physical-d2-gnd` authorization. Hello is
self-reported identity/capabilities, not cryptographic installed-image attestation.
No network upload, managed commit, independent GPIO observation or console-byte
check occurred in this initial setup. Initial ROM verification is distinct from
SDK staging, commit, startup reconciliation and physical iPad/iPhone qualification.

Local checks: `tools/dev.py test --sanitize` passes 20 native cases and 200 host
tests; seven focused composer/ROM-gate cases pass. The initial sandbox invocation
passed native checks but failed 19 existing loopback socket tests on bind
permission; the enabled-loopback rerun passed all 200. The first command remains
a failure. An isolated-interpreter import error in the first composer invocation
was fixed and covered by an actual isolated CLI/no-overwrite test before image
creation. Workflow YAML and all bash run-step syntax pass; actionlint was absent.
CI now exercises the composer with native proof artifacts without a board or
SDK assets. No new resident source/toolchain build or GitHub CI pass is claimed.
