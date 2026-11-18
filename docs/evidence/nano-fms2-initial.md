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

The historical sparse composite above was verified with the ROM protocol
`picotool` uses. The RP2040 USB mass-storage ROM tracks erased sectors by UF2
block number in groups of sixteen. For the later Nano 1.0.0-rc.1 candidate,
the original 602 resident blocks put the first six starter pages in the
resident's last group. An iPad boot-drive write left the resident intact and
the starter sector erased. The composer now emits full physical sectors with
erased padding and starts each at a new group. The reviewed resident and
starter payload bytes are preserved. Completely untouched sectors are left
out of the image. See the [RP2040 ROM implementation](https://github.com/raspberrypi/pico-bootrom-rp2040/blob/master/bootrom/virtual_disk.c).

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
No new local firmware compile matrix is inferred from these checks.
[CI at `6028b6a`](https://github.com/AlexGivens/Firmingo/actions/runs/38822065259)
reports all ten jobs successful, including the new composition step. Public job/
step metadata was inspected; raw logs returned HTTP 403 without credentials,
and the configured GitHub CLI credentials returned HTTP 401. Remote raw logs/
artifacts were not verified; the exact local results above remain separate.

The later [physical iPad record](https://github.com/AlexGivens/FirmingoKit/blob/main/docs/nano-fms2-ipad.json)
now records both exact target-2 SDK uploads in the same boot, one host commit
attempt and fresh boot-confirmed result per sketch, correct initialized data/
startup BSS, advancing counters and two exact 256-byte echoes. Manual D13
observations match State B high and State A low. A separate manual RESET changes
boot identity and passes State A initialization/progress again. The user removed
D2 after completion. These are scoped experimental iPad results, distinct from
initial ROM setup, complete SDK installation, other hosts and production gates.
The later [combined iPad/iPhone record](nano-fms2-hardware.md) also records the
identical signed app's separate iPhone pass and final upload-arm removal.

## Later release-candidate mobile first installation

On 2026-11-18, the selected Nano's non-Firmingo starting condition was established
with a minimal blink sketch built using the pinned Arduino CLI 1.5.1 and
Arduino-Pico 6.0.0. Its UF2 SHA-256 was
`d927b605db3a63f8d68864641299a62d2a3a51c001abf0610548bac56c150b47`.
The ROM flash identity matched the selected board, and a separate 71,168-byte
readback matched every baseline payload byte before reboot. The user then
observed D13 blinking on the iPhone. This baseline is hardware-test preparation.

The user entered ROM mode on an iPhone 15 Pro Max running iOS 27.2.1 and
selected the boot-drive root through Workbench 16. The corrected sector-aligned
319,488-byte candidate composite had SHA-256
`c37f6ad2fba3f0c3071b4446386fec888cd35089ed68ca67d6c7d1d37b87a584`.
Workbench installed it from the iPhone, checked startup, the State A starter
and an exact 256-byte reply, then saved the device association automatically.
The user reported successful installation and D13 off. No new permission prompt
appeared. The initial picker-navigation difficulty was resolved by returning to
Browse/Locations before any write began.

FirmingoKit retains the exact baseline source, binary and app identities,
installation journal and association hashes in
`docs/workbench-mobile-rom-iphone.json`. This result qualifies that selected
Nano/iPhone first-install path. The iPad first-install starting condition,
physically interrupted mobile transfer, broader device matrix and final
post-install flash readback require separate evidence.

The subsequent selected iPad first-install check on 2026-11-18 started with
the same non-Firmingo baseline, freshly programmed and read back over all
71,168 payload bytes. The user observed the blink on an 11-inch iPad Pro
(3rd generation), iPadOS 27.2.1. Workbench 16 installed the same corrected
composite through its in-app boot-drive picker, checked a new boot, the
State A starter and exact 256-byte reply, and saved an association for the
new operation. The new journal differed from the captured prior iPad operation
and boot. The user reported the ready status, exact-reply result and D13 off.
Approval prompts appeared on initial attachment and after the automatic restart.
The user confirmed that no physical reconnect was required after selection.
Prompt wording was not captured, so the permission type is unclassified.

FirmingoKit records the iPad identities, readback, journals and observations in
`docs/workbench-mobile-rom-ipad.json`. First installation from the non-Firmingo
baseline now passes on both selected mobile hosts. Physically interrupted
transfers, the post-install offline multi-file upload, broader hardware matrix
and final post-install flash readback remain separate qualification items.
