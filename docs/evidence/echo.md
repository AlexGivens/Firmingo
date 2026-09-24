# Bounded echo qualification — 2026-09-16

Host: MacBookAir10,1, macOS 27.0 (26A428). Same user-selected Nano RP2040
Connect, serial 3350315009003637; revision and cable/hub details unrecorded.
Arduino-Pico 6.0.0, CLI 1.5.1, pinned board settings, CDC+NCM. NINA unused.
Address 192.168.77.1/24. Firmware UF2 SHA-256:
`bfe4886b0f10d71499064c3459a38434d063c80e812f154ae1b0313abdd44ef5`.

## Problem and change

Before the change, on the local-only image, 256-byte and 4,096-byte echoes passed.
The 65,536-byte case sent everything but received only 2,049 bytes before its
10-second deadline, reproducing the original baseline failure without the
routing problem. The prototype drained receive data in an unbounded loop and
ignored partial returns from potentially blocking WiFiClient writes. No packet
trace was taken, so the exact stack-level cause of the 2,049-byte stopping point
is not established.

Qualification echo now uses the existing portable Stream state machine and a
256-byte loopback backend. A raw lwIP adapter performs at most one read/write per
direction per poll, copies TX bytes into lwIP, preserves unsent tails, and returns
immediately on send-memory exhaustion. TCP output gets one attempt per poll.
All raw API access from the application uses the Ethernet async-context lock;
callbacks already execute in that context. Callbacks only accept/retain input or
clean up; no stream pumping or blocking writes occurs inside them.

The adapter retains one pbuf chain, returns ERR_MEM for a subsequent chain, and
credits bytes only as the application consumes them. lwIP retains refused packets
and bounds receive storage using its configured TCP window/pools. Application
queues total 768 bytes (two stream queues and one loopback batch), in addition to
lwIP's own bounded pools. This is not a measured dynamic-memory high-water mark.

Temporary legacy endpoint semantics: TCP 5001 is raw binary echo, one owner,
no greeting/logs/control bytes. Another client receives TCP RST and cannot replace
the owner. This is transport refusal, **not** the future protocol's structured
BUSY error. EOF/error discards pending data; half-close is unsupported. Thirty
seconds without application transfer progress aborts the connection. Reconnect
starts empty and never replays data. Source5000 and HTTP80 retain prototype behavior.

## Commands and results

```sh
.venv/bin/python tools/dev.py test --sanitize
ARDUINO_CONFIG_FILE="$PWD/arduino-cli.local.yaml" ARDUINO_CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' .venv/bin/python tools/dev.py build --board nano_rp2040_connect --firmware local-only
.venv/bin/python tools/dev.py flash --board nano_rp2040_connect --mount /Volumes/RPI-RP2
.venv/bin/python -m pytest tests/hardware -v -s --board nano_rp2040_connect --address 192.168.77.1 --allow-legacy-no-identity --junitxml=test-results/echo-smoke.xml
```

- Native: 22 Unity cases across stream, TCP adapter and DHCP suites pass with
  ASan/UBSan. New adapter tests cover ERR_MEM, partial writes, refused-packet
  ownership, client exclusion, EOF/error cleanup and idle expiry. Fake lwIP tests
  exercise production adapter code but do not certify the real stack.
- Host harness: 27 tests pass. Initial sandbox invocation denied localhost bind;
  rerunning with socket access passed, without code changes.
- Build: 116,108 bytes program; 76,764 bytes static RAM, within board budgets.
  Existing upstream WiFiClient overloaded-virtual warnings remain.
- Flash: one intentional 1200-baud CDC bootloader request on `/dev/cu.usbmodem101`,
  then explicit ROM-volume/hash-verified copy. No reset/retry was used to turn a
  hardware test failure into a pass. This upload reset is not cold-plug evidence.
- Hardware smoke: **3 passed in 7.36 s**. Source: 5,440 exact bytes in 0.964592 s.
  Echo: 65,536 exact bytes in 3.001362 s (21,835 B/s); reconnect: 4,096 bytes in
  0.358187 s. Concurrent case: 65,536 echo bytes in 3.006749 s and 5,440 source bytes
  in 0.963288 s, all exact.
- Extended check: `echo(..., byte_count=262144, timeout=30, slow_read=True)` in a
  thread alongside `source(..., records=200, timeout=15)` and fresh curl HTTPS
  requests. Echo: 262,144 bytes in 14.843398 s (17,661 B/s). Source: 54,400 bytes in
  11.511741 s. IPv4 and IPv6 HTTPS both returned 200 during the transfers.
- A second echo connection received reset; the original owner then echoed another
  exact message. DHCP still omitted router/DNS, Internet default route remained
  Wi-Fi en0 via 192.168.4.1, board route remained en5.

Extended values and route/DHCP output: [JSON evidence](echo-followup.json).
Local logs: `build/echo-tests.log`, `build/echo-build.log`, `build/echo-smoke.log`,
`test-results/echo-smoke.xml`. Build report includes artifact/source hashes.
Documentation was updated after that build; rebuild before another flash.

## Limits and next step

The 150 ms read pause in smoke does not prove sustained TCP zero-window behavior
because host receive buffering can absorb it. Native tests prove bounded behavior
under simulated TX starvation. Longer on-device backpressure, soak and dynamic
memory stability still need measurement. Throughput is an observation, not a
performance guarantee. HTTP can still block and source writes still need reliable
partial-write handling. No firmware identity endpoint or public control protocol
is implemented; explicit legacy target confirmation was used.

Next: controlled initial USB attachment and repeated macOS/iOS qualification.
No new claim about iOS authorization, no-RESET startup, cellular coexistence,
UART bridging, firmware update or production readiness follows from these tests.
