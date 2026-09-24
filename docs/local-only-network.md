# Local-only network qualification

The old prototype caused a real Internet-access failure on the development Mac.
Its captured DHCP ACK advertised both router and DNS at `192.168.7.1`, and its
`192.168.7.0/24` overlapped the Mac's Wi-Fi `192.168.4.0/22`. The user disconnected
the board to restore Internet access. Keep the preserved baseline for comparison;
do not boot it on the host just to perform this replacement.

`examples/firmingo_local_only/firmingo_local_only.ino` is a qualification fork, not a
production release. It uses the repository's vendored DHCP server, omits router
and DNS options, and configures the board itself without a gateway or DNS server.
No NAT, forwarding or IPv6 advertisement is added. NINA remains unused. It retains
the baseline's source/HTTP behavior. USB now uses the pinned
[controlled NCM-only startup patch](usb-startup.md). Echo uses the
bounded stream core and raw lwIP adapter; [hardware echo checks](evidence/echo.md)
pass. Cold-attachment qualification and source/HTTP backpressure remain unresolved.

## Address and build

Default device address: **192.168.77.1/24**. Lease pool: `.16–.23`. This avoided the
observed host routes when chosen; no fixed private subnet avoids every LAN/VPN.
Multiple boards on the same host remain unsupported. Before using another host,
inspect its routes and select a non-overlapping third octet if necessary.

```sh
.venv/bin/python tools/dev.py build --board nano_rp2040_connect --firmware local-only
# Optional address override: device 192.168.88.1/24, leases .16–.23
.venv/bin/python tools/dev.py build --board nano_rp2040_connect --firmware local-only --subnet-octet 88
```

Both builds require Arduino CLI 1.5.1 and Arduino-Pico 6.0.0. Do not silently
upgrade to the now-installed 6.1.0. `ARDUINO_CONFIG_FILE` selects a separate CLI
configuration without changing the IDE installation. For a clean isolated setup,
create the ignored `arduino-cli.local.yaml` with absolute project paths:

```yaml
directories:
  data: /absolute/path/to/Firmingo/.arduino-data
  downloads: /absolute/path/to/Firmingo/.arduino-downloads
  user: /absolute/path/to/Firmingo/.arduino-user
```

Then explicitly install the pinned core into it with Arduino CLI:

```sh
arduino-cli --config-file arduino-cli.local.yaml core update-index --additional-urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
arduino-cli --config-file arduino-cli.local.yaml core install rp2040:rp2040@6.0.0 --additional-urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
export ARDUINO_CONFIG_FILE="$PWD/arduino-cli.local.yaml"
```

This session instead used the already-cached 6.0.0 archive, verified its pinned
SHA-256 before extracting it into that isolated data directory, copied the package
indexes, and referenced the installed pinned RP2040/builtin tools with symlinks.
No installed core source changed. The build tool now stages the pinned USB
startup patch in a separate build overlay. Build reports include the actual CLI command,
source hashes, image hashes and size checks. Default local-only output is under
`build/firmware/nano_rp2040_connect/local-only/`; the baseline output is separate.

For the IDE, add this repository as a library, open the local-only sketch, and
use the same pinned board settings as the baseline. CLI builds are the recorded
validation path. This change does not solve the core's internal USB reattachment:
the clean upstream 6.0.0 NCM `begin()` still disconnects/registers/connects. No extra
sketch reconnect was introduced, and single stable initial attachment is not claimed.

## Test without a board

```sh
.venv/bin/python tools/dev.py test --sanitize
.venv/bin/cmake -S . -B build/dhcp-fuzz -DFIRMINGO_SANITIZE=ON -DFIRMINGO_DHCP_FUZZ=ON
.venv/bin/cmake --build build/dhcp-fuzz --parallel 2
.venv/bin/ctest --test-dir build/dhcp-fuzz -R dhcp_fuzz --output-on-failure
```

The packet tests exercise the actual server callback and emitted UDP payloads
through fake stack adapters. See `third_party/arduino-pico-dhcp/README.md` for
provenance, supported behavior and limits. Passing native tests does not establish
Internet coexistence on hardware.

## Bootloader-only replacement

Use the Nano's ROM bootloader so the old application never starts:

1. Leave USB disconnected; connect a jumper between **REC and GND**, not RESET.
2. Connect USB. Wait for the **RPI-RP2** drive to appear.
3. Remove the REC–GND jumper, keeping USB connected and the board in bootloader mode.
4. Select this physical Nano and its mounted bootloader volume explicitly:

```sh
.venv/bin/python tools/dev.py flash --board nano_rp2040_connect --mount /Volumes/RPI-RP2
```

The command checks the bootloader's RP2040 family identifier, successful local-only
build report, current source hashes and UF2 hash, then copies only that verified
image. ROM identification does not distinguish every RP2040 board model: physical
selection of the Nano is still required. It never touches a serial port or launches
the old application. Copy completion alone does not prove successful boot.

Arduino documents the REC/GND recovery mechanism in its
[firmware reset instructions](https://support.arduino.cc/hc/en-us/articles/4404168794514-Reset-the-firmware-on-Nano-RP2040-Connect).
Use our qualification UF2, not Arduino's example recovery image. Do not download
or run a flash-erasing utility for this operation.

## Verify coexistence before stress testing

On macOS, identify the new USB interface (do not assume it is always `en5`):

```sh
networksetup -listallhardwareports
ifconfig en5
ipconfig getpacket en5
route -n get 192.168.77.1
route -n get default
scutil --dns
```

Require the assigned `/24` address; DHCP server identifier, subnet and lease;
**no router or DNS options**; the board route over USB; and Internet routing/DNS
through the user's existing connection. Fetch an uncached HTTPS resource using a
fresh query string and `Cache-Control: no-cache`, first alone and then while
checking the deterministic source. Record actual results, not icon changes.
Do not run the heavy echo case until coexistence is established: the baseline
64 KiB echo timed out after only 2,049 verified bytes in the first hardware run.

If Internet access is interrupted, unplug the board and preserve observations.
Do not automatically reset, add host routes, disable Wi-Fi, or retry until a pass.
Repeat the separate iOS/iPadOS checklist from `docs/testing.md`; no macOS check
can certify their authorization or routing behavior. Record exact host/OS, board
revision, firmware hash, cable/hub and successes/attempts (target 20 cold connects
per available target). One upload/reboot does not qualify cold attachment.
