# FMGO framing foundation — 2026-09-18

The user paused repeated physical reconnections at **3 passes / 3 attempts** to
continue firmware development. Remaining macOS/iOS attachment checks remain
incomplete. This increment implements `src/core/protocol.h/.cpp` and the framing
portion of [protocol.md](../protocol.md), matching the existing README proposal:
16-byte FMGO header, major 1, big-endian integers, five frame types and correlated
request IDs. JSON/control/session semantics remain draft and unimplemented.

The decoder owns one fixed 4112-byte frame buffer, accepts arbitrary fragmented
input, stops at a complete frame and reports the consumed prefix for coalesced
input. It rejects magic/version/flags/type/ID/length faults after the header,
before payload collection. Compiled maximum is 4096; peer limits 512–65536
negotiate down to the smaller offer between frames. The encoder verifies output
capacity and header fields before writing. No heap allocation, platform includes
or I/O is used. Wire vectors for hello and channel-1 binary serial data are in
`tests/fixtures/protocol-v1/` and exercised by the actual production codec.

## Commands actually run and final results

```sh
.venv/bin/python tools/dev.py test --sanitize
.venv/bin/cmake -S . -B build/protocol-fuzz -DFIRMINGO_SANITIZE=ON -DFIRMINGO_PROTOCOL_FUZZ=ON -DCMAKE_BUILD_TYPE=Debug
.venv/bin/cmake --build build/protocol-fuzz --parallel 2
.venv/bin/ctest --test-dir build/protocol-fuzz -R protocol_fuzz --output-on-failure
ARDUINO_CONFIG_FILE="$PWD/arduino-cli.local.yaml" ARDUINO_CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' .venv/bin/python tools/dev.py build --board nano_rp2040_connect --firmware local-only
```

- All six native suites pass with ASan/UBSan: 36 Unity cases, including eight new
  framing cases. Every split of representative 0-, 14-, 256- and 4096-byte payloads
  is checked. Tests cover coalesced tails, one-byte feeds, held complete frames,
  all type/ID rules, malformed headers, huge lengths, exact fixtures, encoder
  bounds and negotiated limits. Existing Stream, DHCP, echo and startup tests pass.
- All 60 host tests pass. No hardware tests ran for this new framing component.
- Separate frame mutation job: 20,000 deterministic cases pass, seed `464d4750`,
  in 1.31 s with ASan/UBSan. Mutations vary headers/payloads, truncation, chunk
  sizes and limits, checking consumption/bounds and decoded bytes against input.
  This is bounded mutation testing, not an exhaustive proof or coverage-guided fuzzing.
- Pinned Nano build passes: Arduino-Pico 6.0.0, CLI 1.5.1, existing Nano FQBN and
  NCM-only startup overlay. The generated `core/protocol.cpp.o` confirms board
  compilation of the new source. No new firmware listener uses it yet, so its
  runtime session memory/performance are not established by this build.

Reported program storage: **116,588 bytes**; static RAM: **76,808 bytes**, within
1 MiB / 192 KiB engineering budgets. Six emitted warnings are upstream WiFiClient
overloaded-virtual warnings; no project warnings appeared. UF2: 268,288 bytes,
SHA-256 `7c09dd9a8bbdac9fa236eac1eb5bdffbe9a1f1fafd78c20638919aee463c748f`.
[Saved final build identity and source/artifact hashes](protocol-framing-build.json).
The new library source is compiled but unused codec functions can be discarded
by linking; these image sizes do not budget a future instantiated session.

Local logs: `build/protocol-regressions.log`, `build/protocol-fuzz-build.log`,
`build/protocol-fuzz-results.log`, `build/protocol-firmware-build.log`.
CI includes a separate bounded protocol job but has not been run on GitHub here.
One development fuzz compile failed on mixed integer types in `std::min`; that
was corrected and the final binary rebuilt before recording the mutation result.

No sketch, DHCP, USB descriptors, startup patch or baseline source was changed,
and no board flash/reset or host network changes were performed. The board remains
on its previously recorded `de41aa…` image. Framing validates neither JSON nor
ownership/actions; READY frames must undergo a future semantic layer. No firmware
control, UART, update or Swift interoperability guarantee is added.

Next: bounded JSON schema validation, hello identity/capability serialization,
explicit ownership/error responses, session timers/reconnect cleanup, then TCP
adapter and application-stream reference integration. Hardware qualification
remains separate and paused where requested.
