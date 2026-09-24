# Local-only DHCP qualification evidence — 2026-09-16

## Baseline hardware result before this change

Host: MacBookAir10,1; macOS 27.0 (26A428). User-selected Nano RP2040 Connect,
board revision and cable/hub details not recorded. Original UF2 SHA-256:
`826d811dde1024b2e33b30f1efc7973db7ebad3c2051f8273979c0d7e8b3bcbd`.
The existing core-6.0.0 image was verified before upload. At upload time the
installed IDE core had changed to 6.1.0, so its UF2 copy helper was used; no image
recompilation occurred. This is distinct from the pinned compilation evidence.

After upload the board exposed CDC+NCM, DHCP assigned `192.168.7.16/24` on `en5`,
and the route to `192.168.7.1` selected `en5`. Captured DHCP ACK contained router
and DNS options pointing to `.1`. The host Wi-Fi subnet was `192.168.4.0/22`,
which overlaps the prototype subnet. The user reported losing Internet access
and disconnected the board to restore the chat. No claim of Wi-Fi disassociation
is made. No host network settings were changed by the assistant.

Command actually run:

```sh
.venv/bin/python -m pytest tests/hardware -v -s --board nano_rp2040_connect --address 192.168.7.1 --allow-legacy-no-identity --junitxml=test-results/baseline-smoke.xml
```

Result: **1 passed, 2 failed** in 21.09 s.

- Source: 20 exact records / 5,440 verified bytes in 0.962328 s (5,653 bytes/s).
- Fragmented echo: 65,536 bytes sent, only 2,049 bytes verified before the 10 s
  total deadline. The reconnect portion was not reached.
- Slow-reader/concurrent case: echo connection timed out, 0 bytes sent/received.
  The concurrent source result was not reported after that failure.
- Subsequent bounded TCP connection checks to ports 80, 5000 and 5001 succeeded;
  they do not erase the smoke failures or establish their cause.

Local raw results: `build/baseline-smoke.log`, `test-results/baseline-smoke.xml`,
`build/baseline-upload.log`. No reset/retry was used to turn smoke into a pass.

## Offline local-only firmware change

The original sketch remains byte-for-byte preserved. Qualification fork:
`examples/nano_local_only/`. Device default `192.168.77.1/24`, pool `.16–.23`;
no overlap with the host routes observed while the board was disconnected.
DHCP replies whitelist local address/lease information and never advertise router,
DNS or routes. Vendor provenance, exact patch and limits are recorded under
`third_party/arduino-pico-dhcp/`.

Used a checksum-verified clean Arduino-Pico 6.0.0 archive in `.arduino-data/`,
with pinned installed tools referenced read-only. This avoids silently upgrading
the firmware to the IDE's 6.1.0. Clean upstream NCM attachment behavior remains;
this is not a controlled-initial-attachment implementation.

Commands actually run after the final lease-timing change:

```sh
.venv/bin/python tools/dev.py test --sanitize
.venv/bin/cmake --build build/dhcp-fuzz --parallel 2
.venv/bin/ctest --test-dir build/dhcp-fuzz -R dhcp_fuzz --output-on-failure
.venv/bin/python -m pytest tests/host/test_flash.py -q
ARDUINO_CONFIG_FILE="$PWD/arduino-cli.local.yaml" ARDUINO_CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' .venv/bin/python tools/dev.py build --board nano_rp2040_connect --firmware local-only
```

Results:

- 18 Unity cases passed: 8 stream and 10 DHCP cases, with ASan/UBSan.
- 22 existing host tests passed, plus 5 new flash-gate tests passed separately
  (ordinary temporary files, not a board). Invalid board identifier, failed build,
  changed source and changed image all prevent writes.
- Separate mutation-fuzz job: 20,000 cases, fixed seed `0x464d474f`, passed under
  ASan/UBSan. Not coverage-guided fuzzing or proof of exhaustive parser correctness.
- DHCP tests include exact emitted options, packet fragmentation at every split,
  reordered/PAD/truncated input, malformed headers, retries, renewal/rebinding,
  expiry after long inactivity, pool exhaustion, release/decline, client-ID echo,
  allocation/bind/send errors and unsupported configuration.
- Firmware compile passed, CLI 1.5.1 / core 6.0.0; **114,900 bytes program storage**,
  **75,996 bytes static RAM**, within configured budgets. Dynamic memory not measured.
  Remaining compiler warnings are upstream WiFiClient overloaded-virtual warnings.

Final artifacts:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `nano_local_only.ino.bin` | 132216 | `47edc1c2b882c67c1611aa61268f3aaedf6c9eb98b0dabf950febdecc63f924d` |
| `nano_local_only.ino.map` | 1894118 | `04b3f4c73230fb4144db7bf85f9ab736812ca6473a131f92c800fbed3ba806b6` |
| `nano_local_only.ino.elf` | 2700288 | `13b2f29e4698d62a95ab3a255d4145b0e35234592fc44094e622f4338c2e2eb8` |
| `nano_local_only.ino.uf2` | 264704 | `07b2587e3b699cfd2c70047140747359e7bc743571b7a7aa06d5450c9b1f1631` |

The complete generated report (commands, configuration and input hashes) is at
`build/firmware/nano_rp2040_connect/local-only/report.json`. Artifact size and
compiler storage accounting differ; both are recorded, not conflated.

## Hardware result — one post-flash macOS session

User placed the selected Nano in ROM bootloader mode using REC/GND. The mounted
`INFO_UF2.TXT` reported UF2 Bootloader v2.0, Board-ID `RPI-RP2`. Executed:

```sh
.venv/bin/python tools/dev.py flash --board nano_rp2040_connect --mount /Volumes/RPI-RP2
```

The source and image checksums matched the final report above; UF2 copy succeeded.
The old application was not started to perform this replacement. No serial reset,
manual IP changes, host route changes or automatic recovery retry was used.

Observed on MacBookAir10,1, macOS 27.0 (26A428):

| Check | Actual result |
| --- | --- |
| USB interface | Nano RP2040 Connect, `en5`, active |
| Automatic address | `192.168.77.16`, mask `255.255.255.0` |
| DHCP ACK | Server `192.168.77.1`; lease 600 s; T1 300 s; T2 525 s; client ID echoed |
| Router/DNS DHCP options | Neither option present |
| Route to board | `192.168.77.0/24` through USB `en5` |
| Internet default route | `192.168.4.1` through Wi-Fi `en0`, unchanged from pre-flash |
| System DNS | Existing ISP nameservers scoped to Wi-Fi `en0`; no board DNS |
| Exact source verification | 200 records / **54,400 bytes** in 10.033286 s, 5,422 bytes/s |
| Fresh HTTPS during source | example.com and www.arduino.cc both HTTP **200** over IPv6; request/source intervals overlap |
| Additional IPv4 HTTPS | `curl -4` to example.com returned HTTP **200**; accompanying source check verified another 100 records / 27,200 bytes |

HTTPS requests used a new timestamp query parameter, `Cache-Control: no-cache`,
normal TLS verification, bounded timeouts and no browser cache. They ran on the
actual Mac, not through a remote web-search service. Captured result files:
[coexistence](local-only-coexistence.json) and [IPv4 check](local-only-ipv4.json).
The first file records actual request/source intervals. The supplemental IPv4
file records result retrieval time rather than precise source-thread end time;
use the first file as the timing evidence for concurrent operation.

**Result: desktop USB networking and Internet coexistence passed in this single
post-flash session.** It is not a 20-cycle cold-attachment qualification. Hardware
lease renewal/rebinding, long-term memory stability, denial/locked/first-authorize
paths, iPhone/iPad, and cellular remain untested. Board revision and cable/hub
part numbers remain unrecorded. The previously observed echo failure is not fixed
or claimed to pass; no heavy echo stress was run during this coexistence check.

Subsequent work: the [bounded echo fix](echo.md) passes macOS hardware checks.
The failure above remains the historical result for the earlier image.
Repeated-attachment and iOS coexistence evidence remain pending.
