# Timed macOS USB attachment qualification

Use `tools/dev.py attach` to observe actual USB detach/reattach of one selected
Nano, then qualify its local-only DHCP, identity, routing, binary stream and
Internet coexistence. It is a firmware validation tool, not a desktop client.
It currently supports macOS only. iOS uses the staged manual
[qualification procedure](ios-qualification.md).

The Nano must already be connected with working NCM-only qualification firmware
before arming. Close browser streams and other source/echo clients. Select its
actual USB/diagnostic ID, numeric address and Ethernet interface. The command
checks current identity/network state before asking for physical action.

For this development board and the known flashed image, next cycle 4:

```sh
.venv/bin/python tools/dev.py attach \
  --board nano_rp2040_connect --address 192.168.77.1 \
  --device-id a1b2c3d4e5f60718 --interface en5 \
  --firmware-sha256 de41aa1af5f794d1c7b22e18be4223312ddfb1c4aea1dcf583d348855740c778 \
  --start-cycle 4 --cycles 1 --output build/qualifications/macos-cold-04.json
```

The UF2 hash is **operator-selected metadata**, not a hash read from live flash.
It identifies the image previously flashed/recorded in USB startup evidence.
Another board/image needs its own values. `--connection-details` records user
observations of board revision and cable/hub; its default explicitly says unknown.
Use `--help` for timeouts/options. Existing output files are refused, so failures
cannot be overwritten silently. Checkpoints are saved after each stage.

1. Wait for `Cycle N: armed`, then physically unplug the Nano's USB cable.
2. Reconnect the cable. Approve a legitimate host prompt if one appears; record
   whether it appeared, what action was taken, and lock/authorization state.
3. Wait for functional PASS or the first failure. Never press RESET, change IP
   settings or reconnect again to rescue the same cycle.
4. A batch prints the next armed cycle only after the prior checks finish. Do not
   rapidly cycle the cable while traffic is running; unobserved actions cannot be
   counted as qualification evidence.

For the remaining 17 cycles, use `--cycles 17 --start-cycle 4` and a new output path
such as `build/qualifications/macos-cold-04-through-20.json`. An observed timeout,
identity mismatch, unsafe DHCP, route change or byte failure stops the batch.
Recovery/new runs are separate attempts with separate files. A timeout waiting
for the user to unplug is `not_started`, not a failed hardware connection.
Ctrl-C preserves the latest checkpoint.

## What the tool measures

The tool samples the USB registry every approximately200 ms and selects only the
Nano's VID/PID/serial, then requires disappearance and a different registry
instance on reappearance. An observation-command error stops the run; it is never
interpreted as an absent device. USB registry output from unrelated devices is
not saved. Observed detach/reattach alone cannot prove a physical power cycle;
the user must supply the physical action and note any external power.

Timing begins at the end of the first sample containing the selected USB serial.
`observed_usb_to_address_seconds` ends when the active interface and local DHCP ACK
are observed; `observed_usb_to_identity_seconds` ends after the one successful
identity/diagnostic request. Sample-start/end and last-negative-sample times are
stored. These are **host-observed readiness intervals**, not exact physical
insertion, packet-level DHCP lease, USB configuration or authorization times.
An ACK may be cached; transaction-ID change is recorded but is not required because
DHCP clients may legally reuse identifiers. Exact DHCP exchange timing needs a
separate scoped capture; see testing.md.

Default deadlines:120 s for unplug,120 s for reconnect,30 s for address/identity.
They bound this test, not a firmware latency promise. A slow legitimate approval
can consume the readiness budget and must remain in the outcome. Address/USB
readiness are polled within one deadline; identity, byte checks and Internet
requests are not retried. The tool never flashes, resets, changes network settings
or suppresses host authorization.

After identity/startup validation, the tool verifies concurrent65,536-byte echo
with a brief read pause and100 exact source records, plus a4,096-byte reconnect.
Fresh IPv4 and default-family HTTPS requests must both complete inside the source
and echo intervals. Default-family can be IPv4 or IPv6; recorded remote IP reveals
which was used. All bytes are checked, and Internet results/timing are saved.
Internet/default-route changes invalidate the cycle even if caused externally;
inspect the evidence before attributing a failure to firmware.

Approval prompts, physical insertion/lease timestamps, board revision, external
power, cable/hub and first/previous authorization remain manual observations.
Copy reviewed results into docs/evidence and update the ledger with actual
attempt/pass counts. Simulated tests of this harness are not hardware passes.
Current ledger: [macOS cold checks](evidence/macos-cold.md).
