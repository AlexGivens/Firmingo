# Raspberry Pi Pico port evidence

Date: 2026-09-23

## Port boundary and target selection

The Raspberry Pi Pico was selected as the second direct board because the user
already has the board and it exposes the RP2040's native USB device controller.
It also has a smaller 2 MiB flash device, different board definitions and USB
identity, no NINA Wi-Fi module, and a BOOTSEL recovery path. Those differences
exercise the board manifest, build selection, memory budget, reported board
identity, recovery instructions, and UART pin documentation while retaining the
existing Arduino-Pico transport adapter.

This is a second **board configuration**, not a second silicon family or SDK
adapter. Both boards use the RP2040 and the pinned Arduino-Pico 6.0.0 core. A
future target from another MCU family is still needed to validate the abstraction
against a distinct USB/IP implementation.

## Pinned configuration

- Board ID: `raspberry_pi_pico`
- FQBN: `rp2040:rp2040:rpipico:usbstack=picosdk,flash=2097152_0,freq=125,ipbtstack=ipv4only,opt=Small,rtti=Disabled,exceptions=Disabled`
- Arduino CLI: 1.5.1
- Arduino-Pico: 6.0.0
- Layout: 2 MiB, no filesystem; 2,093,056-byte maximum sketch and 4,096-byte
  EEPROM reservation
- Engineering budgets: 1,048,576 flash bytes and 196,608 static RAM bytes
- USB profile: NCM only with the same reviewable deferred-start overlay
- UART0 reference pins: GP0 TX, GP1 RX, plus common ground; 3.3 V logic

The common board identity header maps Arduino-Pico's
`ARDUINO_RASPBERRY_PI_PICO` definition to `raspberry_pi_pico`. An undeclared
Arduino-Pico board fails compilation instead of reporting a misleading identity.

## Compile evidence

All three shared reference profiles compiled on 2026-09-23. Compilation proves
that the declared Pico configuration fits; it does not prove USB attachment,
DHCP, routing, TCP behavior, UART electrical behavior, or recovery on hardware.

| Profile | Flash | Static RAM | UF2 SHA-256 |
| --- | ---: | ---: | --- |
| Application | 125,876 B | 88,160 B | `56cf986f5b07f538aa37f62d7859ec06ee7c4e53142eb2995d4ef249e7d2490a` |
| Local-only qualification | 119,580 B | 77,108 B | `1ada43b3ff680814caf28302a888c31c0a876989662d1723d0d697034520b274` |
| UART bridge | 126,572 B | 87,384 B | `c35da9011a5ca96412c3951c4fa163c4c51f75ae2eb7f79b938b31f067484b20` |

The reports preserve commands, source hashes, patched-core hashes, artifacts,
and size summaries:

- `raspberry-pi-pico-application-build.json`
- `raspberry-pi-pico-local-only-build.json`
- `raspberry-pi-pico-uart-build.json`

Arduino-Pico emitted its existing `WiFiClient` hidden-overload warnings. No
Firmingo compilation error occurred.

The post-change regression command was:

```sh
.venv/bin/python tools/dev.py test --sanitize
```

All 10 sanitizer-enabled CTest executables and all 157 host tests passed. The
host tests include a regression proving that Pico reference selection does not
try to read the Nano-only preserved-baseline fields. The first hardware-smoke
launch then exposed a second explicit allowlist in the pytest hardware plugin:
argument parsing rejected `raspberry_pi_pico` before collecting or running any
traffic test. The plugin now consumes the declared board list, a collection-only
regression covers the Pico value, and the full suites pass after the correction.

## Flash and recovery limits

Enter ROM recovery by holding BOOTSEL while connecting USB, then release it when
the `RPI-RP2` volume appears. The RP2040 ROM's `INFO_UF2.TXT` reports the generic
`RPI-RP2` Board-ID on both the Pico and Nano. The flash gate verifies a selected
RP2040 bootloader volume, current source hashes, requested firmware profile, and
UF2 hash, but it cannot prove which RP2040 carrier is attached. The operator must
explicitly select `--board raspberry_pi_pico` and prepare the physical Pico.

Hardware qualification starts with the local-only image. The Nano must be
disconnected because both default builds use `192.168.77.1/24`. A pass requires
automatic NCM attachment and DHCP, diagnostics reporting
`raspberry_pi_pico`, exact qualification traffic, and preserved host Internet
routing. Application FMGO smoke follows only after that safer network check.

## First local-only hardware qualification

The user disconnected the Nano and inter-board UART wires, then placed the Pico
in BOOTSEL ROM mode. The host mounted `RPI-RP2`, and its `INFO_UF2.TXT` reported
`UF2 Bootloader v2.0`, model `Raspberry Pi RP2`, and Board-ID `RPI-RP2`. The flash
tool rechecked every recorded source hash and UF2 hash before copying the
local-only image. This exercises ordinary BOOTSEL recovery and application boot;
it is not a repeated or power-interruption recovery test.

The Pico automatically attached to macOS 27.0 on a MacBookAir10,1 as `en14` and
received `192.168.77.16/24`. No RESET or manual network setting was used. The
diagnostics endpoint reported:

- board `raspberry_pi_pico`
- device ID `B2C3D4E5F6071829`
- NCM-only USB initialized after `setup()`
- DHCP and NCM ready at 2 ms, services ready and attachment requested at 3 ms

The local-only hardware smoke passed all three cases: exact deterministic source
records, a fragmented 65,536-byte echo followed by an exact 4,096-byte reconnect,
and a concurrent slow-reader 65,536-byte echo while the source remained active.

A separate bounded coexistence run verified the decoded DHCP ACK, routes, exact
traffic, and fresh Internet requests. The ACK supplied the server identifier,
`255.255.255.0` mask, 600-second lease, 300-second T1, and 525-second T2. It
supplied no router, DNS, classless-route, or static-route option. The board route
used `en14`; the default route remained Wi-Fi `en0` through `192.168.4.1`.
While a 65,536-byte exact slow-reader echo and 27,200 exact source bytes were in
flight, fresh IPv4 and IPv6 HTTPS requests both returned HTTP 200. A subsequent
4,096-byte echo reconnect passed, and DHCP, routes, and identity remained stable.

Raw results: [local-only hardware JSON](raspberry-pi-pico-local-only-hardware.json).

## Current status

The application image was then installed through a second deliberate BOOTSEL
cycle. It automatically regained `192.168.77.16/24` on `en14` without RESET or
manual settings and reported FMGO identity `raspberry_pi_pico`, device ID
`b2c3d4e5f6071829`, firmware `application-dev-v7`, and application channel 1.

The first application-smoke invocation exposed a host-only portability bug before
opening a socket: the FMGO harness accepted only lowercase hexadecimal device
IDs, while the Pico qualification endpoint had displayed the same ID in uppercase.
The harness and soak validator now accept either case and canonicalize to lowercase.
Two focused regressions pass, including an ID containing `A`–`F`.

The corrected hardware smoke passed:

- second-client `busy` enforcement
- 16,384 fragmented exact bytes and 4,096 exact bytes with a delayed reader
- zero pending bytes after 20,480 bytes in each backend direction
- explicit `unsupported` reset rejection without resetting the board
- 4,096 exact bytes after ownership transfer
- 4,096 exact bytes through a fresh TCP session after both earlier closures

A 15.307-second diagnostics-required run then passed 180,224 exact bytes in 11
batches across six closed sessions. Heap free/minimum stayed at 173,728 bytes;
approximate core-0 stack free/minimum stayed at 8,056 bytes. Application pending
and discarded byte counts and TCP errors remained zero, while all queue peaks
stayed at their 256-byte bound. These are sampled values, not exhaustive memory
stability evidence. Raw report: [application soak JSON](raspberry-pi-pico-application-soak.json).

A subsequent 61.552-second run passed 622,592 exact bytes in 38 batches across
10 closed sessions. The same heap and stack values remained unchanged; pending
and discarded application bytes, TCP errors, NCM mutex contentions, budget
exhaustions, and wake requests stayed zero. NCM receive batch peak remained 2.
Raw report: [one-minute application soak JSON](raspberry-pi-pico-application-soak-60s.json).

Finally, one seeded 65,536-byte exact FMGO transfer fully contained fresh IPv4
and IPv6 HTTPS requests, both HTTP 200. The board route stayed on `en14`, the
Internet default stayed on Wi-Fi `en0`, the application queues drained to zero,
and pre/post diagnostics retained the same sampled memory minima with zero TCP
errors. Raw report:
[application coexistence JSON](raspberry-pi-pico-application-coexistence.json).

The Pico local-only and application profiles are hardware-verified for these
single macOS sessions. UART remains compile-tested only. Repeated cold attachment,
Apple mobile-device authorization, long-duration traffic, exhaustive memory stability,
and recovery under interruption remain untested. The CI matrix declares all
three Pico builds, but that GitHub workflow has not yet run.
