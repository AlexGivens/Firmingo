# Application diagnostics — 2026-09-18

Status: **flashed; macOS application smoke and required-diagnostics two-minute
soak pass**. Previous failures and traffic-only results remain separate evidence.
The user prepared RPI-RP2 using REC/GND; its INFO_UF2.TXT identified the RP2040
ROM bootloader. No new physical cold-attachment qualification cycles were added.

The application adds bounded C-heap/core-0-stack samples before/after server work,
lifetime sampled minima and persistent portable Stream queue peaks. A new
`device.diagnostics` request is available after hello without taking the channel;
unsupported ports/no samples return an explicit error. Existing hello/status and
raw application bytes remain unchanged. `soak --diagnostics` requires initial and
per-batch snapshots, validates bounds and lifetime monotonicity, and records them
without retries or claiming leak freedom. See [wire meanings/limits](../protocol.md#sampled-runtime-diagnostics-experimental-additive-method).

## Commands actually run

- `.venv/bin/python tools/dev.py test --sanitize`: eight native suites pass with
  ASan/UBSan; 112 host tests pass. Localhost sockets required sandbox escalation;
  the first restricted run failed socket setup, not firmware assertions.
- `.venv/bin/cmake --build build/native-sanitize --parallel 2` and
  `.venv/bin/ctest --test-dir build/native-sanitize --output-on-failure`: final
  regression pass includes shared diagnostics wiring through the raw TCP port.
- `.venv/bin/cmake -S . -B build/diagnostics-fuzz -DFIRMINGO_SANITIZE=ON -DFIRMINGO_PROTOCOL_FUZZ=ON -DCMAKE_BUILD_TYPE=Debug`,
  build targets `protocol_fuzz session_fuzz`, then CTest selecting those targets:
  both bounded mutation jobs pass with sanitizers.
- Pinned `tools/dev.py build --board nano_rp2040_connect --firmware application`
  and `--firmware local-only`, with local CLI/config environment as documented in
  [development](../development.md): both pass. Application program 118,140 B,
  static RAM 86,600 B; qualification program 116,604 B,
  static RAM 76,816 B. Upstream WiFiClient overloaded-virtual
  warnings remain; no project warning observed.
- SHA-256 checks verify every build-recorded source/artifact and preserve the
  baseline checksum. `git diff --check` passes.

Application UF2: `f553b19b18b2b265e042c38ef33cead60dd0c01244caa48c001c00b0c21850d4`.
Qualification fallback UF2: `95fbb246c4f6d44477a8741999222436a2ef59598ee35018e6a4cd4a31a6f807`.
Build reports: [application](application-diagnostics-build.json),
[qualification](diagnostics-qualification-build.json).
Logs: [combined tests](diagnostics-tests.log),
[final native](diagnostics-native-final.log), [mutation checks](diagnostics-fuzz.log).

## Installation procedure used

The Nano was manually placed in ROM recovery using REC/GND, then the verified
application image was flashed to the selected volume. Application smoke preceded
the measured soak with `--diagnostics`, verified board ID and the hash above.
The board remains a local-only endpoint; no host network edits or new physical
cold-attachment qualification cycles were performed. iOS, a real application/UART
backend, lwIP pool measurements and exhaustive stack high-water are unvalidated.


## Hardware commands and measurements

Target: Nano RP2040 Connect, ID `a1b2c3d4e5f60718`, USB Ethernet `en5`, address
`192.168.77.1`; MacBookAir10,1, macOS 27.0 (26A428). Board revision, cable/hub,
and authorization state were not newly recorded. Firmware hash is the verified
flashed UF2 above; the hello reply does not attest its hash on-device.

```sh
.venv/bin/python tools/dev.py flash --board nano_rp2040_connect --firmware application --mount /Volumes/RPI-RP2
.venv/bin/python tools/dev.py smoke --board nano_rp2040_connect --firmware application --address 192.168.77.1 --device-id a1b2c3d4e5f60718
.venv/bin/python tools/dev.py soak --board nano_rp2040_connect --address 192.168.77.1 --device-id a1b2c3d4e5f60718 --firmware-sha256 f553b19b18b2b265e042c38ef33cead60dd0c01244caa48c001c00b0c21850d4 --duration 120 --byte-count 16384 --batch-timeout 20 --reconnect-every 4 --diagnostics --output build/application-diagnostics-soak-120s.json
```

Flash checked current source/image hashes, selected ROM identity, copied and
fsynced the UF2. Full smoke passes 28,672 exact bytes, fragmented/full-duplex and
delayed-reader traffic, BUSY, unsupported reset without resetting, ownership
transfer and confirmed-close fresh reconnect. Counters RX=TX=20,480 and pending
queues zero before ownership transfer.

The measured soak ran 121.849257 seconds, UTC 2026-09-18T22:50:23Z to
2026-09-18T22:52:25Z: **1,245,184 exact bytes**, 76 seeded 16 KiB batches and
19 confirmed-closed TCP sessions. Boot ID `b10b16d1750787d0` matches smoke and
remains unchanged across reconnects. Backend counters match each session's exact
bytes and queues are empty after each batch. 64 detailed samples are retained;
12 additional details are omitted by the fixed cap, with the last sample retained.
No failed test was retried or rescued by reset. No host network settings changed.

| Measurement (bytes unless stated) | Initial | Final |
| --- | ---: | ---: |
| Sample count | 81,297 | 301,619 |
| C heap free | 175,292 | 175,292 |
| C heap sampled minimum | 175,292 | 175,292 |
| Approximate core-0 stack free | 8,120 | 8,120 |
| Stack sampled minimum | 8,120 | 8,120 |
| Stream queue peak to backend | 256 | 256 |
| Stream queue peak to peer | 256 | 256 |

This establishes stable **sampled values during this bounded run**, not general
leak freedom or deepest-stack safety. The C heap excludes lwIP's fixed pool;
`lwip_free` remains null. Stack samples surround server work and cannot measure
deepest parser/callback/interrupt usage. Queue peaks exclude backend/framing/lwIP
buffers. Longer runs and real backend traffic remain needed.

DHCP ACK assigns Mac `.16/24` without router option 3 or DNS option 6. Default
route remains Wi-Fi `en0` via `192.168.4.1`. A fresh IPv4 HTTPS fetch while the soak
command was active returns HTTP 200 in 0.388496 seconds; this check does not trace
exact overlap with a particular payload interval. A separate exact 64 KiB transfer also passes on this image, with HTTP 200
completed entirely inside its measured transfer interval. iOS routing/authorization
remains untested.

Raw evidence: [smoke](application-diagnostics-smoke.log),
[soak JSON](application-diagnostics-soak-120s.json), [soak log](application-diagnostics-soak-120s.log),
[network/DHCP](application-diagnostics-network.log),
[Internet fetch](application-diagnostics-soak-https.json).

Next: integrate a real application backend with explicit bounded queues and
lifecycle behavior. Hardware UART remains an optional separate capability and
must not be advertised without actual implementation and validation.


## Exact-transfer Internet coexistence follow-up

Actual command: `PYTHONPATH="$PWD" .venv/bin/python build/application-diagnostics-coexistence-check.py`.
The retained script derives from the preceding timer-service check, with this
image's hash/output paths, unchanged boot assertion and before/after diagnostics.
One attempt passed; no reset/retry or host route change. UTC 2026-09-18T22:52:33Z to
2026-09-18T22:52:39Z; 65,536 exact bytes, 500 fragmented sends, delayed reader,
5.996996 seconds (10928.1 B/s). Fresh IPv4 HTTPS returns HTTP 200
in 0.348109 seconds, entirely contained in the transfer interval. RX=TX=65,536,
pending queues zero, unchanged boot and memory samples. Post-test default route
remains `en0` via `192.168.4.1`; the board route remains `en5` and DHCP omits router/DNS.

Raw evidence: [coexistence JSON](application-diagnostics-coexistence.json),
[script](application-diagnostics-coexistence-check.py),
[post-test network](application-diagnostics-network-final.log).
