# iPhone and iPad USB-Ethernet qualification

This manual procedure qualifies USB CDC-NCM attachment, local-only DHCP, browser
stream integrity, and Internet coexistence on one explicitly recorded iOS or
iPadOS device. It does not certify the FMGO raw-TCP protocol; that requires a
separate socket client and the production application/UART image.

## Image and bench preparation

Use the report-verified `local-only` profile. Record its UF2 SHA-256 from
`build/firmware/nano_rp2040_connect/local-only/report.json`; do not infer live
flash identity from the address alone. The image exposes only NCM USB, supplies a
local `192.168.77.0/24` lease without router or DNS options, and serves:

- `http://192.168.77.1/` — exact deterministic browser-stream validation.
- `http://192.168.77.1/diagnostics` — firmware, board, stable device ID, and
  startup timestamps.

Remove the Pico peer before this test. With both boards unpowered, disconnect TX,
RX, and ground and set the Pico aside. Do not add a Nano TX/RX loopback for the
browser attachment test. Connect only the Nano to the iPhone or iPad with a
data-capable cable or powered adapter appropriate to the device. Record every
cable, hub, adapter, and external-power detail.

## First diagnostic attempt

Keep the target unlocked with Wi-Fi enabled. Start from the Nano unpowered, then
connect it once. Do not press RESET, reconnect the cable, or edit IP settings to
rescue the attempt. A legitimate authorization prompt may be accepted; record the
exact prompt and response. Do not reset privacy settings to manufacture a prompt.

1. Start a timer at physical connection.
2. Wait for iOS/iPadOS to expose the Ethernet interface and automatic address.
3. Record address and mask. Expected: `192.168.77.x/24`.
4. Record Router and DNS. Expected: empty; `0.0.0.0` is not an acceptable value.
5. Open `http://192.168.77.1/diagnostics` in Safari. Record time to first valid
   response and require device ID `a1b2c3d4e5f60718` on this development Nano.
6. Open `http://192.168.77.1/`. Require green `exact records validated`, a
   continuously increasing record count, and no sequence/header/payload error.
   Let it run for at least 30 seconds and record final records and bytes.
7. While the board stream remains active, open a new Safari tab and load an
   uncached public HTTPS resource over Wi-Fi. Record success/failure and whether
   Wi-Fi remained associated. A Wi-Fi icon alone is not evidence.
8. On a capable iPhone, repeat Internet access with Wi-Fi disabled so cellular is
   the only Internet path, while retaining the USB-Ethernet connection. Restore
   Wi-Fi afterward. If the plan does not include cellular, record `not tested`.

A legitimate authorization prompt on every physical connection is acceptable
when that is the host's policy. Record each prompt and response; do not attempt to
bypass or suppress it. A manual RESET/IP change, missing lease, unreachable board,
browser validation error, additional prompt after an accepted connection, or loss
of the selected Internet path fails the attempt. Recovery belongs in a separately
numbered attempt; it never converts a failure into a pass.

## Staged scenarios

Begin with three attempts rather than immediately repeating twenty times:

| Attempt | Initial state | Required observation |
| --- | --- | --- |
| 1 | Unlocked cold connection | Automatic lease, diagnostics, 30-second exact browser stream, Wi-Fi Internet |
| 2 | Previously authorized reconnect | Record any prompt; after acceptance require automatic addressing and a fresh exact stream with no rescue action |
| 3 | Connect while locked, then unlock | Normal authorization and automatic service after unlock |

If all three are stable, later qualification can cover a board reset while
attached and a total of 20 cold connections per available device. Denial and
first-authorization behavior are recorded only when naturally reproducible.

An SDK cannot make data flow while iPadOS is waiting for the user to authorize
the accessory. After acceptance, it may monitor interface/reachability and open a
fresh Firmingo connection automatically. It must verify stable device identity,
perform a new hello/open exchange, and must not replay commands whose completion
was uncertain when the previous TCP session ended. That SDK is a separate host
deliverable; this repository only defines and validates the firmware behavior it
will consume.

## Evidence fields

For every attempt record: attempt number and result; iPhone/iPad model; exact OS
version/build; firmware UF2 SHA-256; board/device ID; cable/adapter/hub and power;
lock and prior-authorization state; prompt/response; connection time; time to
automatic address; time to diagnostics; address/mask/router/DNS; final validated
record/byte count and duration; Wi-Fi Internet result; cellular result when
applicable; any reset, reconnect, setting change, timeout, or recovery action.

Desktop packet captures cannot observe the iPhone's USB exchange. Diagnose a
failure in order: power/reset, USB configuration, NCM interface, DHCP/IP,
HTTP/TCP, exact browser records, then Internet routing.
