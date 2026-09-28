# Nano managed-sketch M9 hardware — 2026-09-27

The user authorized M9 testing before tonight's planned M8 overnight check,
then instructed that M8 must not be restored until they explicitly request it.
The exact historical `m8exp4` recovery image remains preserved. This record concerns
the experimental image, not release firmware or iOS qualification.

## Initial ROM installation and console

The selected Arduino Nano RP2040 Connect appeared as `RPI-RP2`. The pinned
`pqt-picotool/5.0.0-9576866` read ROM flash ID `5031503337360009` before writing.
`flash_initial.py --board nano_rp2040_connect --sketch blink_b` used the isolated
`build/managed_sketch/m9exp1/` resident/module/composite directories, checked
current source and artifact hashes, loaded the composite, verified every UF2
block during load and again with `picotool verify`, then rebooted successfully.

The composite SHA-256 was
`f33d894bb9ed02ddf410cff36550cecd217c889d0b9c95a5f6f1244e121ca4aa`.
Resident and capsule identities are in the [build record](managed-sketch-m9exp1.md).
Board revision and cable/hub details remain unrecorded. This is the same macOS
host used for the preceding M8 tests; its last recorded OS was macOS 27.0,
build 26A428.

`inspect_hello.py --address 192.168.77.1 --timeout 10` reported firmware
`0.1.0-m9exp1`, board `nano_rp2040_connect`, device `5031503337360009`, and boot
`cfe57d2cb3cc2daf`. Target 1 advertised `nano-managed-v1`, 4096-byte stage,
1024-byte chunks, and `physical-d2-gnd` authorization.

`smoke.py --address 192.168.77.1 --device-id 5031503337360009 --sketch blink_b
--resident-version m9exp1` passed: 16,384 fragmented exact echo bytes,
4,096 slow-reader bytes, second-client busy refusal, 4,096 ownership-transfer
bytes, and 4,096 fresh-session reconnect bytes. Total: **28,672 exact bytes**.
The first echo took 0.826 s; the slow-reader echo took 0.490 s. During the
first two echoes, sketch loops advanced from 17,995 to 19,117, epoch and
acknowledgement both remained one, and sketch input/output discarded stayed
zero. Startup output rejection stayed 16; output peak reached 128. Sampled
heap/stack minima were 165,020/3,968 bytes; these are shallow diagnostic samples,
not exhaustive stack or long-duration memory bounds.

With D2 unbridged, a valid `flash.begin` for the exact 688-byte Blink A capsule
returned `unauthorized` on the same boot. The check used the production Probe
and `verified_capsule` with the build record's exact resident/capsule hashes.
No commit was sent. The user subsequently bridged D2–GND and confirmed D13
was blinking; the exact initial rate was not specified in that reply.

## First network installation and pre-commit disconnect

With the physical jumper in place, the explicit `fmgo_upload.py` transfer used
the exact resident UF2 hash from the build record and Blink A capsule SHA-256
`4e92dbd5a9d24952c712bd32d8348a6d0a3de5c54441e42bd8908822564ad3b7`.
Transaction 1 received all 688 bytes, verified the RAM stage, acknowledged the
single commit, and reached `boot_confirmed`, `slot_touched:true`, `error:none`.
The resident boot ID remained `cfe57d2cb3cc2daf`; no ROM entry, reset, or manual
host network changes occurred. Confirmation means the new generation's setup
and first loop returned, not independently measured LED behavior.

Full Blink A console smoke passed all **28,672 exact bytes** on that boot,
including busy refusal, ownership transfer, and reconnect. The first echo took
2.571 s; slow-reader echo took 0.499 s. Loop boundaries advanced from 1,222,105
to 1,224,950 during the first two echoes. Discard counters remained zero and
startup rejection stayed 32 (16 more bytes from Blink A setup); output peak
reached the 256-byte capacity without loss. Sampled heap/stack minima remained
165,020/3,968 bytes.

Next, `fmgo_upload.py transfer --sketch blink_b --stage-only` validated the exact
692-byte Blink B capsule, SHA-256
`6b5ce8500f31ee623a04d4a5264e5543281cd569ab5149f7c30e54712d518a81`,
and deliberately disconnected after verification without sending commit.
Read-only reconciliation on the same boot reported transaction 2 as `aborted`,
all 692 bytes received, `slot_touched:false`, and `error:disconnected`.
A subsequent fragmented **4,096-byte exact echo** passed in 0.582 s; sketch
loops advanced from 1,305,660 to 1,306,198 with discard/rejection counters
unchanged. This is a network disconnect before flash, not a power-cut test.

An additional real-board check rejected a wrong board (`wrong_target`), a
4097-byte stage (`insufficient_storage`), upload while console-owned (`busy`),
console open and a second begin while upload-active (`busy`), a non-owner abort
(`wrong_owner`), and early finish (`incomplete`). Transaction 3 then accepted
the Blink B capsule in 41 chunks of at most 17 bytes, with the last byte
deliberately corrupted against its advertised SHA-256. Finish returned
`digest_mismatch`; another session's status reported `failed`, all 692 bytes
received, `slot_touched:false`, `error:digest_mismatch`. No commit was sent.
After failure, console ownership became available and a 4,096-byte exact echo
passed in 0.491 s on the same resident boot.

The user confirmed Blink A's D13 LED blinked approximately 250 ms per state.

## Second installed variant and discarded commit result

The next explicit harness install used the exact Blink B capsule above.
Transaction 4 received all 692 bytes and reached `boot_confirmed`,
`slot_touched:true`, `error:none`; its commit result was received normally.
The resident boot stayed `cfe57d2cb3cc2daf`. Together with transaction 1, this
demonstrates successive A then B network replacements without ROM recovery,
reset, or manual host network changes.

Full Blink B console smoke again passed **28,672 exact bytes**, busy refusal,
ownership transfer, and reconnect. First echo took 0.826 s; slow-reader echo
took 0.487 s. During those echoes, loop boundaries advanced from 1,554,236 to
1,555,345, epoch/ack both were 21, discards stayed zero, and startup rejection
stayed 48. Sampled heap/stack minima remained 165,020/3,968 bytes; output peak
was 256.

Two bounded fault-injection checks then used the production host `transfer`
function with a Probe subclass intercepting only `flash.commit`. Each checked
the exact resident/capsule hashes, sent one framed commit, closed the original
socket without decoding a commit result, and let `transfer` open a new session
and query status. Neither resent commit. Both used Blink B again so the visible
half-period would remain unchanged.

- **Transaction 5:** immediately sending FIN after the commit frame yielded no
  response bytes. Reconciliation did not report success; the test's assertion
  expecting `boot_confirmed` failed. A separate `fmgo_upload.py status
  --upload-id 5 --boot-id cfe57d2cb3cc2daf` confirmed `aborted`, all 692 bytes
  staged, `slot_touched:false`, `error:disconnected`. This is consistent with
  disconnect before commit dispatch, since the port does not support half-close;
  no packet trace was taken to identify exact timing. No commit was replayed.
- **Transaction 6:** the subclass sent the commit frame, waited up to three
  seconds with `select` for the socket to become readable, and closed without
  reading or decoding any response bytes. The production harness's uncertainty
  path then queried status on the same boot and obtained `boot_confirmed`, all
  692 bytes received, `slot_touched:true`, `error:none`. The captured operation
  list was `hello, flash.begin, flash.finish, flash.commit, hello, flash.status`
  (binary chunks are outside that command list). Exactly one commit was sent.
  This is deliberate client-side result loss, not a physical USB-disconnect or
  power-cut observation.

Read-only status retained transaction 6, and a subsequent **4,096-byte exact
echo** passed in 0.574 s. Sketch loops advanced from 1,660,714 to 1,661,245;
epoch/ack were both 28, discards stayed zero, and startup rejection stayed 64.
Sampled heap/stack minima remained 165,020/3,968 bytes. TCP error count was one
before and after that echo; the deliberately closed unread socket is consistent
with that error, but its cause was not independently traced.

The user confirmed Blink B's D13 blink was approximately 500 ms per state.
Both successive installed variants now have separate console and operator LED
observations. At this point power-cut cases were pending; the subsequent
verified-stage test is recorded below. M9 stays installed until the user
requests restoration. Firmware source was unchanged through these hardware
checks; no new compilation or native-test pass is claimed.

## Manual power-interruption procedure

AGENTS.md requires power-cut cases to be explicitly selected on a recoverable
board, separate from smoke. The proposed first case is power loss after RAM
verification, before commit, on this same Nano (`5031503337360009`). It does
not test interruption during erase/program and cannot establish atomic updates.

1. Verify the selected M9 device/boot and exact Blink A capsule/build hashes.
   Keep D2–GND bridged. The installed image is Blink B, with the observed 500 ms
   half-period. Preserve the existing M9 initial composite for ROM recovery.
2. Begin, transfer, and finish Blink A into RAM only. Record the new upload ID,
   boot ID, exact size/hash, `verified`, and `slot_touched:false`. Do not send
   `flash.commit`. Hold the owner session for at most five minutes with periodic
   read-only status requests, stopping on disconnect; expiration closes it and
   disposes the stage. No background reconnect or replay is permitted.
3. Once RAM verification is reported, the user disconnects USB power, waits
   five seconds, and reconnects normally. Do not enter REC/GND ROM mode. The
   user must confirm the board has no other power source for this to count as
   power loss rather than a transport-only disconnect.
4. After the user confirms reconnection, inspect identity and the new boot ID,
   confirm Blink B's 500 ms blink, and run exact console smoke. RAM transaction
   status is expected to be lost. Do not interpret an old transaction ID as a
   result on a different boot and do not replay commit.
5. Record the actual outcome, including failures or recovery. If startup fails,
   use the separately documented REC/GND recovery and the exact M9 composite
   after ROM identity/hash checks; M8 restoration still requires the user's
   request. Further network installation after the power cycle is a separate
   explicit transfer, not continuation of the old upload.

Loss of power during actual flash write is a separate case. Manual unplugging
does not reliably select the short erase/program interval; no timing result or
mid-write recovery claim is made by this prepared case.

## Verified RAM stage and manual power interruption

The user explicitly selected this case and confirmed USB is the Nano's only
power source. The staging check revalidated the exact M9 resident and Blink A
capsule reports/hashes and the selected device/boot. Transaction 7 received all
688 bytes and reached `verified`, SHA-256
`4e92dbd5a9d24952c712bd32d8348a6d0a3de5c54441e42bd8908822564ad3b7`,
`slot_touched:false`, `error:none`, on boot `cfe57d2cb3cc2daf`.
The check contains no `flash.commit` call. It holds the owner session for at
most 300 seconds with one read-only status request per second, stopping at
connection loss or closing without commit on expiry. A bounded preparation
record is saved under ignored
`build/managed_sketch/m9exp1/hardware/verified-stage-power-cut.json`.

The user was instructed to unplug USB, wait five seconds, and reconnect
normally without ROM mode. They confirmed disconnect/reconnect and D13's
continued 500 ms half-period. The hold check ended with a socket timeout and
recorded `connection_lost`; it did not expire normally or send a commit.
The power-off duration was not independently measured.

`inspect_hello.py --address 192.168.77.1 --timeout 10` then reported the same
M9 version/device/board/target with new boot `8a2ce145a65c7a51`. Before creating
another upload, a read-only `flash.status` for old ID 7 returned `wrong_target`,
as expected with no retained transaction on the new boot. This is evidence of
RAM status loss, not reconciliation of an old outcome across boot IDs. No
commit was replayed.

Full Blink B console smoke passed **28,672 exact bytes**, busy refusal,
ownership transfer, and reconnect on the new boot. First echo took 0.823 s;
slow-reader echo took 0.493 s. Loop boundaries advanced from 87,670 to 88,790
during those echoes. Epoch/ack both were one, discards remained zero, and
startup output rejection stayed 16. Output peak was 128; sampled heap/stack
minima remained 165,020/3,968 bytes. No ROM recovery, board reset command, or
manual host network setting changes were needed.

A separate fresh `fmgo_upload.py transfer --sketch blink_b --install` then
used the same exact hashes to verify the updater remained usable after power
loss. Its new-boot transaction ID was 1, all 692 bytes received, state
`boot_confirmed`, `slot_touched:true`, `error:none`, with normal commit reply.
The boot ID remained `8a2ce145a65c7a51`. ID reuse after reboot is intentional;
transaction IDs must always be interpreted within their boot context.
After this fresh install, another **4,096-byte exact echo** passed in 0.574 s.
Loops advanced from 140,255 to 140,788; discards stayed zero and startup
rejection stayed 32. Sampled heap/stack minima were again 165,020/3,968 bytes.
The bounded result is saved under ignored
`build/managed_sketch/m9exp1/hardware/post-power-cut-install.json`.

This one selected power-loss case preserved the previously installed Blink B,
discarded a verified but uncommitted Blink A RAM stage, and permitted a fresh
network install. It does not test power loss during sector erase/page program,
establish rollback, or resolve the earlier overnight failure. M9/Blink B remains
installed; further power-cut stages and overnight reliability are untested.
