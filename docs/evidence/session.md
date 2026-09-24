# Portable JSON/application session — 2026-09-18

Implemented the application session portion of [protocol.md](../protocol.md)
on the existing FMGO frame codec. `core/json.*` validates strict bounded JSON;
`core/session.*` serializes identity/capabilities, negotiates payload limits,
acquires/releases one shared application Channel and transfers binary payloads
through the existing production Stream. All code is independent of Arduino,
lwIP, TinyUSB and board headers, with no production heap allocation.

The JSON validator bounds tokens (96), nesting (eight containers) and decoded
strings (64 UTF-8 bytes). It validates syntax, UTF-8 and Unicode escapes, rejects
isolated surrogates/embedded NUL and compares duplicate object keys after decoding.
Known command schemas reject extra fields, wrong types and integer overflow.
Open/configure/control validation precedes any operation. UART/reset/runtime/
upload capabilities are absent and receive explicit unsupported responses.

Hello reports caller-supplied stable device ID, per-boot ID, board/firmware names,
negotiated limit, open-development auth profile, one application channel and no
programming targets. It acquires no channel. The port still must generate boot
identity and verify its own board identity source; native fixture identities are
test data. No cryptographic authentication or hardware integration is claimed.

Channel uses the same Stream as existing native/echo tests, adds saturating u64
backend counters, and disposes only session queues through StreamBackend::discard.
Loopback implements discard by clearing its bounded batch. A newcomer gets a
correlated busy response without affecting the owner. Queue saturation retains
tails and the other direction can progress. Command replies/backend output share
bounded turns. Ownership/disconnect cleanup and hello/frame/idle/error-flush
deadlines are enforced in Session with caller-provided wrapping milliseconds.

Exact shared hello/reset response vectors were added under
`tests/fixtures/protocol-v1/`; native tests compare actual output bytes. FMGO
framing is unchanged. New JSON schemas remain experimental rather than frozen.
Reset's response acknowledges rejection only, and status is not a DATA completion
barrier. See the contract for precise counter/acknowledgement semantics.

## Commands actually run

```sh
.venv/bin/cmake -S . -B build/native-sanitize -DFIRMINGO_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
.venv/bin/cmake --build build/native-sanitize --parallel 2
build/native-sanitize/session_tests
.venv/bin/python tools/dev.py test --sanitize
.venv/bin/cmake -S . -B build/protocol-fuzz -DFIRMINGO_SANITIZE=ON -DFIRMINGO_PROTOCOL_FUZZ=ON -DCMAKE_BUILD_TYPE=Debug
.venv/bin/cmake --build build/protocol-fuzz --parallel 2
.venv/bin/ctest --test-dir build/protocol-fuzz -R '(protocol|session)_fuzz' --output-on-failure
ARDUINO_CONFIG_FILE="$PWD/arduino-cli.local.yaml" ARDUINO_CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' .venv/bin/python tools/dev.py build --board nano_rp2040_connect --firmware local-only
```

Final results:

- **Seven native suites / 57 Unity cases pass with ASan/UBSan**, including 21 new
  session/JSON cases. Coverage includes JSON grammar/Unicode/duplicates/limits,
  integer boundaries, every split of representative hello/open/reset requests,
  coalesced input, all 1–256-byte partial write sizes, exact 4092-byte maximum
  serial input, negotiated overflow before payload read, binary transparency,
  busy/unsupported/malformed requests, offline backend and faulty adapter counts,
  timeout boundaries/rollover, both stall directions, reconnect/destructor/close
  cleanup, status semantics, fairness and at most one I/O call per direction/poll.
- **All 60 host tests pass**. These are existing harness checks; the new protocol
  has no board TCP listener to test yet.
- Separate frame mutation: **20,000 cases**, seed `464d4750`, passes in 1.19 s.
  Separate JSON/session mutation: **20,000 cases**, seed `53455331`, passes in
  1.80 s. It varies valid/mutated/truncated commands and serial frames, read/write
  chunks, stalls and wrapping clocks, checks bounds/released ownership and validates
  complete emitted response/serial frames. Standalone JSON mutations inspect
  token/string bounds. These bounded jobs are not exhaustive or coverage-guided.
- Pinned Nano qualification build passes (Arduino-Pico 6.0.0 / CLI 1.5.1 /
  established FQBN and NCM-only overlay). Generated json.cpp.o/session.cpp.o confirm
  board compilation. Reported program storage **116,604 B**, static RAM **76,808 B**,
  within 1 MiB / 192 KiB budgets. Six emitted warnings are upstream WiFiClient
  overloaded-virtual warnings; no project warnings appeared.

UF2 size 268,288 B, SHA-256
`07d0ac5ba7c465d8ca30c978085515d7cd8bcc8bf285b250b957f79aa714d2d2`.
[Saved final build identity and hashes](session-build.json). The reference sketch
does not instantiate Session yet; unused methods can be discarded by the linker.
These image sizes do not budget a future live TCP service or its stacks/queues.

A separate native arm64 sizeof probe, compiled with `clang++ -std=c++11 -I src`,
reported Session 4832 B, Channel 592 B, Document 1568 B and Loopback 280 B.
These are host layouts, not RP2040 RAM evidence. Session retains decoder/output
buffers; the JSON Document and bounded recursive parser also require application
stack headroom during dispatch. Measure that in the integrated board build.

Local logs: `build/session-focused.log`, `build/session-regressions.log`,
`build/session-fuzz-build.log`, `build/session-fuzz-results.log`,
`build/session-firmware-build.log`. CI includes both mutation jobs but has not
been run on GitHub here. Development failures resolved before final checks:
C++11 required an out-of-class Stream::capacity definition after odr-use; ASan
found a test helper retaining a temporary string after destruction (the Document
borrows input, now explicit in its contract); the empty-container nesting boundary
was corrected with focused regression checks.

No flash/reset/host network changes or hardware checks were performed for this
component. The current board stays on its previously recorded `de41aa…` image;
the baseline, sketch, DHCP and USB startup patch are preserved. Cold qualification
remains paused at 3/3 and iOS remains untested for the new startup image.

Next: a bounded raw-lwIP adapter with explicit connection limits and pbuf/PCB
lifetime handling, followed by reference firmware integration and actual identity/
binary/Internet smoke checks after separately installing that image. No UART,
runtime console, firmware update or Swift interoperability is advertised.
