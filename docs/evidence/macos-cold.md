# macOS cold-connection checks — 2026-09-16–18

Target: Nano RP2040 Connect, diagnostic/USB ID `a1b2c3d4e5f60718`.
Host: MacBookAir10,1, macOS 27.0 (26A428). Board revision and cable/hub details
remain unrecorded. Same NCM-only image as the startup report, UF2 SHA-256
`de41aa1af5f794d1c7b22e18be4223312ddfb1c4aea1dcf583d348855740c778`.
Arduino-Pico 6.0.0, CLI 1.5.1, Pico SDK USB; NINA unused.

## Cycle ledger

| Cycle | Physical action / authorization | Automatic network result | Exact bytes / Internet | Result |
| --- | --- | --- | --- | --- |
| 1 | User reported unplug/replug; no macOS approval prompt | NCM bound on en5; DHCP 192.168.77.16/24, no router/DNS; correct stable ID | All 3 smoke cases pass; additional concurrent source/echo and fresh IPv4/IPv6 HTTPS pass | Functional pass; lease latency unmeasured |
| 2 | User reported unplug/replug; no macOS approval prompt | New USB instance; local DHCP and correct stable ID; host-observed USB-to-address 2.864 s, USB-to-identity 2.882 s | Exact concurrent echo/source, reconnect and fresh IPv4/IPv6 HTTPS pass | Functional pass |
| 3 | User replied to unplug/replug request: no macOS approval prompt | New USB instance; local DHCP and correct stable ID; host-observed USB-to-address 2.926 s, USB-to-identity 2.956 s | Exact concurrent echo/source, reconnect and fresh IPv4/IPv6 HTTPS pass | Functional pass |

Count: **3 functional passes / 3 physical attempts toward a 20-cycle macOS target**.
iPhone/iPad: zero attempts. No reset, reflash or host network changes were performed
by the assistant during this cycle. The requested user action was only unplug/replug;
no RESET or manual IP change was requested. The USB registry device instance changed
from the preceding session, and the DHCP ACK transaction ID changed to `0x8da4fc25`.

Checks began after the user reported reconnection. Physical insertion time, time
to driver binding and time to lease were **not measured**. This functional pass
does not establish a maximum attachment time. Absence of a new prompt on a Mac
that has already used this board is not first-authorization evidence.

## Cycle 1 commands and observations

```sh
.venv/bin/python tools/dev.py smoke --board nano_rp2040_connect --address 192.168.77.1 --device-id a1b2c3d4e5f60718
```

Before test bytes, inspected ioreg, ifconfig en5, ipconfig getpacket en5 and routes
to the board/default Internet destination; read identity through `/diagnostics`.
The bound interfaces were NCM control0/class2/subclass13 and data1/class10;
no CDC serial interface. Wi-Fi en0 via192.168.4.1 remained the Internet default.

Board-relative diagnostics: setup1 ms, DHCP-ready1 ms, NCM-ready2 ms,
listeners-ready2 ms, attach-request2 ms; USB initialized at setup was false.
These describe board readiness, not host lease or authorization latency.

Smoke: **3 passed in 8.24 s**:

- Source: 5,440 exact bytes in 0.991664 s.
- Echo: 65,536 exact bytes in 3.277815 s (19,994 B/s); reconnect4,096 bytes
  in 0.360872 s.
- Concurrent case: echo65,536 exact bytes in 3.561817 s (18,400 B/s),
  source5,440 bytes in 1.464880 s.

Additional independent check used timed threads for
`echo(..., byte_count=65536, timeout=10, slow_read=True)` and
`source(..., records=100, timeout=10)`, alongside fresh curl HTTPS requests:

- Echo65,536 exact bytes in 4.351127 s (15,062 B/s).
- Source27,200 exact bytes in 6.033281 s.
- IPv4 and default IPv6 HTTPS each returned200 while both stream threads ran.

Performance varies between runs; no minimum throughput is asserted. The short
read pause does not qualify sustained TCP zero-window behavior or memory stability.

[Raw cycle record, diagnostics, routes, DHCP and concurrent timing](macos-cold-cycle-01.json).
Local smoke log: `build/macos-cold-cycle-01-smoke.log`. No firmware/source changes
were made for this check. Native/build results remain those recorded in
[USB startup evidence](usb-startup.md).

## Timed cycle 2 — 2026-09-18 UTC

Ran the following command before asking the user to unplug/replug:

```sh
.venv/bin/python tools/dev.py attach --board nano_rp2040_connect --address 192.168.77.1 --device-id a1b2c3d4e5f60718 --interface en5 --firmware-sha256 de41aa1af5f794d1c7b22e18be4223312ddfb1c4aea1dcf583d348855740c778 --start-cycle 2 --cycles 1 --unplug-timeout 300 --plug-timeout 120 --output build/qualifications/macos-cold-02-20260918.json
```

The process exited successfully after one functional pass, without reset, flash,
host network edits or retries. USB registry instance changed; DHCP ACK transaction
ID changed to `0xdce9016f`, assigning `192.168.77.16/24` without router or DNS
options. The Internet default remained Wi-Fi en0 via `192.168.4.1`.
Diagnostics verified the same board/device ID and NCM-only startup ordering.

Host-observed USB-to-address was 2.863500 s and USB-to-identity 2.881861 s.
These intervals begin at host registry appearance, not physical insertion, and
do not measure packet-level lease time. See the [measurement limits](../cold-attachment.md).

- Concurrent echo: 65,536 exact bytes in 3.759619 s (17,432 B/s).
- Concurrent source: 27,200 exact bytes in 5.561939 s.
- Reconnect: 4,096 exact bytes in 0.357469 s.
- Fresh IPv4 and IPv6 HTTPS each returned 200 within both stream intervals.

The user explicitly reported physical unplug/replug and no macOS approval prompt.
This is previously used Mac/board reconnection evidence; first authorization and
iOS behavior remain untested. Board revision, cable/hub and external-power details
remain unrecorded. No throughput threshold or sustained memory-stability claim
is made. [Reviewed raw cycle record with user observation](macos-cold-cycle-02.json).
The original generated record remains at the command's output path.

## Timed cycle 3 — 2026-09-18 UTC

Ran the same single-cycle attachment command with `--start-cycle 3` and new output
`build/qualifications/macos-cold-03-20260918.json`. It exited successfully, without
reset, flash, host network edits or retries. USB instance changed; DHCP ACK ID
changed to `0x5585a3be`. The address remained `192.168.77.16/24` without router/DNS
options, and Internet default remained Wi-Fi en0 via `192.168.4.1`.

Host-observed USB-to-address: 2.925600 s; USB-to-identity: 2.956284 s.
The timing limits described for cycle 2 apply here too.

- Concurrent echo: 65,536 exact bytes in 3.872935 s (16,922 B/s).
- Concurrent source: 27,200 exact bytes in 5.482490 s.
- Reconnect: 4,096 exact bytes in 0.358085 s.
- Fresh IPv4 and IPv6 HTTPS each returned 200 within both stream intervals.

The user reported no approval prompt in response to the physical reconnection
request. First authorization and iOS behavior remain untested. No firmware was
changed. [Reviewed raw cycle record with user observation](macos-cold-cycle-03.json).

Repeated attachment paused by user on 2026-09-18 to continue firmware development.
Remaining qualification: 17 further physical macOS connections with readiness timing,
and available iPhone/iPad routing/authorization checks. Keep the earlier post-upload
timeout visible; this later successful physical cycle does not erase it.
