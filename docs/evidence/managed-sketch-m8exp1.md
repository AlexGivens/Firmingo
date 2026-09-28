# Nano managed-sketch M8 console candidate — 2026-09-25

Board target: Arduino Nano RP2040 Connect, Arduino-Pico 6.0.0, Pico SDK USB
stack, NCM only. The `m8exp1` image was installed and revealed a console
handoff failure. It must not be treated as an accepted M8 build.

## Candidate and host checks

The resident now uses the production `SketchConsole` adapter between the
application endpoint on core 0 and the constrained `Serial` facade on core 1.
The adapter has two 256-byte SPSC queues. A new stream owner waits for a core-1
loop boundary, which prevents the previous owner's loop output from reaching
the new owner. The new diagnostic group reports bridge queue disposal,
refused/short writes, and output high-water mark. Existing FMGO framing and
base diagnostic fixture bytes remain unchanged.

Commands actually run:

| Command | Result |
| --- | --- |
| `cmake --build build/native --parallel 2` | Pass |
| `ctest --test-dir build/native --output-on-failure` | 19/19 CTest targets pass, including bridge and `Serial` facade tests |
| `ctest --test-dir build/native-m8-sanitize -R '^(managed_console\|managed_serial_facade\|session)$' --output-on-failure` | 3/3 selected targets pass with address/undefined-behavior sanitizers |
| `.venv/bin/python -m pytest -q tests/host` | 169 passed with loopback permission; first sandbox attempt could not bind `127.0.0.1` |
| `experiments/managed_sketch/build_resident.py` with pinned CLI/overlay and `--output-dir build/managed_sketch/m8exp1/resident` | Pass; 132,680 flash bytes, 93,936 static RAM bytes |
| `experiments/managed_sketch/build_module.py --sketch blink_b` against that resident ELF | Pass; 436 code bytes, 692-byte capsule |
| `experiments/managed_sketch/build_composite.py --sketch blink_b` with M8 paths | Pass; 304,128-byte UF2 |
| `git diff --check` | Pass |

Only the pre-existing Arduino-Pico `WiFiClient::write(uint8_t)` hidden-overload
warning appeared in the pinned resident compile log. No project-source
warning was found.

A separate `tools/dev.py build --board nano_rp2040_connect --firmware
application` check did not reach compilation: the existing M8 overlay is
already patched while that command expects a clean upstream file, and the
default global Arduino core is 6.1.0 rather than pinned 6.0.0. This attempt
does not establish reference-image build status. The M8 resident build above
used the pinned 6.0.0 overlay successfully.

Exact artifacts under ignored `build/managed_sketch/m8exp1/`:

| Artifact | SHA-256 |
| --- | --- |
| Resident ELF | `94788ea30dfe6e504e36ebda6ea81cfcd00eec1aeb4211073763b0a8e311b06d` |
| Resident UF2 | `792a3646e762d617f04cc29495e752d108cef792f099bf889514fdb3121d2824` |
| Blink B capsule | `3d1ecd77a3920d2bc9a517b434109bf638f8b441f544ae10ed7ff9741b09d21c` |
| Full resident plus Blink B ROM UF2 | `c3f2f41537c76d1836aab7cd9f41556a455d01f722068ec04db201b919feab9c` |

## First Nano run: failed echo

On 2026-09-26 the Nano appeared as `RPI-RP2`. Pinned `picotool info -a`
reported RP2040 B1, 16 MiB flash, and flash ID `5031503337360009`, matching
the previously selected proof board. `flash_initial.py` regenerated the exact
`c3f2...ab9c` composite, checked the flash ID, loaded and verified the full
UF2, verified it again, then rebooted. The new FMGO hello reported
`0.1.0-m8exp1`, expected board/device ID, and boot ID
`3612475170a4089b`.

The first `smoke.py --resident-version m8exp1` sent 16,384 echo bytes but
received zero before its deadline. No successful byte echo is claimed. A
subsequent one-byte probe showed `backend_rx_bytes:1`,
`application_rx_pending:1`, `application_tx_peak:0`, and
`sketch_output_peak:0`: FMGO accepted input but the bridge did not pass it to
the sketch. `sketch_output_rejected:16` reflects Blink B's setup print before
opening the console. The board remained reachable with the same boot ID.
Whether D13 continued blinking is awaiting visual confirmation. This failure
does not identify the exact scheduler or epoch cause.

## Diagnostic follow-up candidate

`m8exp2` adds individually sampled sketch loop-boundary, owner-epoch,
acknowledged-epoch, and enable-state diagnostics. It was flashed on the same
Nano and exposed a stopped sketch loop counter before the new owner was
acknowledged; see the [diagnostic run](managed-sketch-m8exp2.md). The
`m8exp3` follow-up avoids unnecessary atomic read-modify-write operations.

## Hardware acceptance still open

- Test the `m8exp3` candidate on the selected Nano after a fresh ROM flash-ID
  check; verify that loop and epoch counters advance and exact bytes return.
- Establish the handoff cause and verify a passing exact image.
- Compare `sketch_*` counters during a slow-reader echo and owner transfer.
- Unplug/replug and repeat the byte/counter check with a changed boot ID.
- Verify an uncached Internet resource still loads over Wi-Fi while the local
  USB console runs; a Wi-Fi icon alone is insufficient.
- Exercise an IDE reconnect without resetting or reflashing. No IDE-specific
  integration has been implemented or tested in this repository.
